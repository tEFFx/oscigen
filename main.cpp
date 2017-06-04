#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <climits>
#include <cmath>
#include <sstream>

#define SAMPLES_PER_FRAME 1470
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

void drawWaveform(sf::RenderTarget& target, sf::SoundBuffer& buffer, int playbackPos, const char index, const char count, const int length);
short averageSample(const short* samples, const unsigned int startIndex, const unsigned char count);
int renderRealtime(const std::vector<sf::SoundBuffer*>& buffers, const short bufSize);
int renderImages(const std::vector<sf::SoundBuffer*>& buffers, const short bufSize, const char* masterAudio);

int main(int argc, char* argv[])
{
	int mode = 0;

	std::string outputFile = "output";
	std::vector<std::string> inputFiles;

	std::string prevCmd = "";
	for(unsigned char i = 0; i < argc; i++) {
		std::string cmd = std::string(argv[i]);

		if(cmd[0] != '-') {
			if(prevCmd == "-i") {
				inputFiles.push_back(std::string(argv[i]));
			} else if(prevCmd == "-o") {
				outputFile = std::string(argv[i]);
			}
		} else {
			if(cmd == "-i" || cmd == "-o") {
				prevCmd = std::string(argv[i]);
			} else if(cmd == "-r") {
				mode = 1;
			}
			else if(cmd == "-?") {
				std::cout << "HALP" << std::endl;
			}
		}
	}

	std::vector<sf::SoundBuffer*> buffers;
	for (unsigned char i = 0; i < inputFiles.size(); i++) {
		sf::SoundBuffer* buffer = new sf::SoundBuffer();
		if(buffer->loadFromFile(inputFiles[i])) {
			buffers.push_back(buffer);
		} else {
			std::cout << "ERROR: " << inputFiles[i] << " is either invalid or does not exist." << std::endl;
		}
	}

	int bufSize = buffers.size();
	if(bufSize == 0) {
		std::cout << "ERROR: No valid input file(s) specified!" << std::endl;
		return 0;
	}

	switch(mode) {
		case 0:
			renderImages(buffers, bufSize, inputFiles[0].c_str());
			break;
		case 1:
			renderRealtime(buffers, bufSize);
			break;
	}

	return 0;
}

void drawWaveform(sf::RenderTarget& target, sf::SoundBuffer& buffer, int playbackPos, const char index, const char count, const int length) {
	float interval = WINDOW_WIDTH / (float)length;
	float waveHeight = WINDOW_HEIGHT * (1.0f / count);
	float invAmplitude = (1.0f / SHRT_MAX) * waveHeight;
	float verticalCenter = WINDOW_HEIGHT / (count + 1) + waveHeight * index;
	if(count > 1)
		verticalCenter -= waveHeight * 0.25f;

	const unsigned int channels = buffer.getChannelCount();
	const short* samples = buffer.getSamples();
	playbackPos *= channels;

	unsigned short centerSnap = 0;
    unsigned short maxAmplitude = 0;
	for (unsigned short i = 0; i < length * 2; i += channels) {
		short sample = averageSample(samples, playbackPos + i + length, channels);

		if(sample >= maxAmplitude) {
			maxAmplitude = sample;
			centerSnap = i;
		}
	}

	playbackPos += centerSnap;
	if(centerSnap < 0)
		centerSnap = 0;

	sf::Vertex vertices[length];
	for (unsigned short i = 0; i < length; i++) {
		short sample = averageSample(samples, playbackPos + i * channels, channels);

		vertices[i] = sf::Vector2f(i * interval, verticalCenter + sample * invAmplitude);
	}

	target.draw(vertices, length, sf::LinesStrip);
}

short averageSample(const short* samples, const unsigned int startIndex, const unsigned char count) {
	short result = 0;
	for (unsigned int i = startIndex; i < startIndex + count; i++) {
		result += samples[i];
	}

	return result / count;
}

int renderRealtime(const std::vector<sf::SoundBuffer*>& buffers, const short bufSize) {
		sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Oscigen");

		sf::Sound bgMusic;
		bgMusic.setBuffer(*buffers[0]);
		bgMusic.play();

		while (window.isOpen()) {
			sf::Event event;
			while (window.pollEvent(event))	{
			    if (event.type == sf::Event::Closed)
					window.close();
			}

			window.clear();

			int playbackPos = bgMusic.getBuffer()->getSampleRate() * bgMusic.getPlayingOffset().asSeconds();
			if(bufSize > 1) {
				for (unsigned char i = 0; i < bufSize - 1; i++) {
					drawWaveform(window, *buffers[i + 1], playbackPos, i, bufSize - 1, SAMPLES_PER_FRAME);
				}
			} else {
				drawWaveform(window, *buffers[0], playbackPos, 0, 1, SAMPLES_PER_FRAME);
			}
			window.display();
		}

		bgMusic.stop();

		return 0;
}

int renderImages(const std::vector<sf::SoundBuffer*>& buffers, const short bufSize, const char* masterAudio) {
	std::ostringstream cmd;
	cmd << 	"ffmpeg -r 60 -f rawvideo -pix_fmt rgba -s 1280x720 -i - -i " << masterAudio << " -strict -2 " <<
			"-threads 0 -preset fast -y -pix_fmt yuv420p -crf 21 output.mp4";
	FILE* ffmpeg = popen(cmd.str().c_str(), "w");

	const unsigned short samplesPerFrame = buffers[0]->getSampleRate() / 60;
	const unsigned int numFrames = (buffers[0]->getSampleCount()) / samplesPerFrame;
	std::cout << "Preparing to render " << numFrames << " frames" << std::endl;

	sf::RenderTexture frame;
	frame.create(WINDOW_WIDTH, WINDOW_HEIGHT);

	unsigned int currentFrame = 0;
	while(currentFrame < numFrames) {
		unsigned int sampleIndex = currentFrame * samplesPerFrame;

		frame.clear();

		if(bufSize > 1) {
			for (unsigned char i = 0; i < bufSize - 1; i++) {
				drawWaveform(frame, *buffers[i + 1], sampleIndex, i, bufSize - 1, samplesPerFrame);
			}
		} else {
			drawWaveform(frame, *buffers[0], sampleIndex, 0, 1, samplesPerFrame);
		}

		frame.display();

		sf::Image img = frame.getTexture().copyToImage();
		fwrite(img.getPixelsPtr(), WINDOW_WIDTH*WINDOW_HEIGHT*4, 1, ffmpeg);

		currentFrame++;
	}

	pclose(ffmpeg);

	return 0;
}
