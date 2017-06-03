#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <climits>
#include <cmath>

#define SAMPLES_PER_FRAME 735
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

void drawWaveform(sf::RenderWindow& window, sf::SoundBuffer& buffer, int playbackPos, const char index, const char count);
short averageSample(const short* samples, const unsigned int startIndex, const unsigned char count);

int main(int argc, char* argv[])
{
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
			} else if(cmd == "-?") {
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
				drawWaveform(window, *buffers[i + 1], playbackPos, i, bufSize - 1);
			}
		} else {
			drawWaveform(window, *buffers[0], playbackPos, 0, 1);
		}
		window.display();
	}

	bgMusic.stop();
	return 0;
}

void drawWaveform(sf::RenderWindow& window, sf::SoundBuffer& buffer, int playbackPos, const char index, const char count) {
	float interval = WINDOW_WIDTH / (float)SAMPLES_PER_FRAME;
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
	for (unsigned short i = 0; i < SAMPLES_PER_FRAME * 2; i += channels) {
		short sample = averageSample(samples, playbackPos + i + SAMPLES_PER_FRAME, channels);

		if(sample >= maxAmplitude) {
			maxAmplitude = sample;
			centerSnap = i;
		}
	}

	playbackPos += centerSnap;
	if(centerSnap < 0)
		centerSnap = 0;

	sf::Vertex vertices[SAMPLES_PER_FRAME];
	for (unsigned short i = 0; i < SAMPLES_PER_FRAME; i++) {
		short sample = averageSample(samples, playbackPos + i * channels, channels);

		vertices[i] = sf::Vector2f(i * interval, verticalCenter + sample * invAmplitude);
	}

	window.draw(vertices, SAMPLES_PER_FRAME, sf::LinesStrip);
}

short averageSample(const short* samples, const unsigned int startIndex, const unsigned char count) {
	short result = 0;
	for (unsigned int i = startIndex; i < startIndex + count; i++) {
		result += samples[i];
	}

	return result / count;
}
