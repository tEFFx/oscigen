#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <climits>
#include <cmath>
#include <sstream>

//Vector helper macros
#define length(a) (float)sqrt((a).x*(a).x + (a).y*(a).y)
#define normalize(a) (a) / length(a)
#define dot(a,b) ((a).x*(b).x)+((a).y*(b).y)
#define project(a,b) (a)*(dot(a,b)/(length(a)*length(a)))

//Math helper macros
#define sign(a) (a==0 ? 0 : (a>0 ? 1 : -1))
#define clamp(a,b,c) ((a)<(b)?(b):((a)>(c)?(c):(a)))

//TODO: Screen resolution should not be constant and probably be exposed as a command line argument
#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

void drawWaveform(sf::RenderTarget& target, sf::SoundBuffer& buffer, int playbackPos, const uint8 index, const uint8 count, const uint32 length);
short averageSample(const short* samples, const uint32 startIndex, const uint8 count);

int main(int argc, char* argv[])
{
	bool preview = true;
	//TODO: Add command line argument to change this
	int sampleLen = 1500;

	std::string outputFile = "output.mp4";
	std::vector<std::string> inputFiles;

	std::string prevCmd = "";
	for(uint8 i = 1; i < argc; i++) {
		std::string cmd = std::string(argv[i]);

		if(prevCmd != "" && cmd[0] != '-') {
			if(prevCmd == "-i") {
				std::string input = std::string(argv[i]);
				inputFiles.push_back(input);
				std::cout << "Input:" << input << std::endl;
			} else if(prevCmd == "-o") {
				outputFile = std::string(argv[i]);
				std::cout << "Output: " << outputFile << std::endl;
			}
		} else {
			//TODO: Add command line argument for gain
			if(cmd == "-i" || cmd == "-o") {
				prevCmd = std::string(argv[i]);
			} else if(cmd == "-h") {
				preview = false;
			} else if(cmd == "-?") {
				std::cout 	<< "Oscigen commands" << std::endl << std::endl
							<< "-i\tSpecifies audio files used. First is master audio." << std::endl
							<< "-o\tOutput file. Default is \"output.mp4\"." << std::endl
							//TODO: Fix headless mode
							<< "-h\tHeadless mode, disables preview window. (broken)" << std::endl
							<< "-?\tPrints all available commands." << std::endl;
				return -1;
			} else {
				std::cout << cmd << " is not a recognised command! Use -? for list of available commands." << std::endl;
				return -1;
			}
		}
	}

	std::vector<sf::SoundBuffer*> buffers;
	for (uint8 i = 0; i < inputFiles.size(); i++) {
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
		return -1;
	}

	//TODO: Figure out a way to enable anti-aliasing without using a RenderWindow
	sf::ContextSettings settings(8, 8, 8);
	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Rendering...", sf::Style::Default, settings);
	sf::Texture capture;
	capture.create(WINDOW_WIDTH, WINDOW_HEIGHT);

	std::ostringstream cmd;
	cmd << 	"ffmpeg -r 60 -f rawvideo -pix_fmt rgba -s 1280x720 -i - -i " << inputFiles[0] << " -strict -2 " <<
			"-threads 0 -preset fast -y -pix_fmt yuv420p -crf 21 " << outputFile;
	FILE* ffmpeg = popen(cmd.str().c_str(), "w");

	const uint16 samplesPerFrame = buffers[0]->getSampleRate() / 60;
	const uint32 numFrames = buffers[0]->getSampleCount() / buffers[0]->getChannelCount() / samplesPerFrame;
	std::cout << "Preparing to render " << numFrames << " frames" << std::endl;

	//TODO: Rendering could probably be faster if FFMPEG piping is on another thread than rendering
	uint32 currentFrame = 0;
	while(currentFrame < numFrames && window.isOpen()) {
		sf::Event event;
		while(window.pollEvent(event)){
			if(event.type == sf::Event::Closed)
				window.close();
		}


		window.clear();

		uint32 sampleIndex = currentFrame * samplesPerFrame;
		if(bufSize > 1) {
			for (uint8 i = 0; i < bufSize - 1; i++) {
				drawWaveform(window, *buffers[i + 1], sampleIndex, i, bufSize - 1, sampleLen);
			}
		} else {
			drawWaveform(window, *buffers[0], sampleIndex, 0, 1, sampleLen);
		}

		window.display();

		capture.update(window);
		sf::Image img = capture.copyToImage();
		fwrite(img.getPixelsPtr(), WINDOW_WIDTH*WINDOW_HEIGHT*4, 1, ffmpeg);

		currentFrame++;
		int progress = ceil(((float)currentFrame / (float)numFrames) * 10000) / 100;
		std::ostringstream title;
		title << "oscigen (" << progress << "%)";
		window.setTitle(title.str());
	}

	pclose(ffmpeg);
	window.close();

	return 0;
}

void drawWaveform(sf::RenderTarget& target, sf::SoundBuffer& buffer, int playbackPos, const uint8 index, const uint8 count, const uint32 length) {
	float interval = WINDOW_WIDTH / (float)length;
	float waveHeight = WINDOW_HEIGHT * (1.0f / count);
	float invAmplitude = (1.0f / SHRT_MAX) * waveHeight;
	float verticalCenter = WINDOW_HEIGHT / (count + 1) + waveHeight * index;
	if(count > 1)
		verticalCenter -= waveHeight * 0.25f;

	const uint32 numSamples = buffer.getSampleCount();
	const uint32 channels = buffer.getChannelCount();
	const short* samples = buffer.getSamples();
	playbackPos *= channels;

	uint16 centerSnap = 0;
	while(averageSample(samples, playbackPos + centerSnap + length, channels) > 0)
		centerSnap += channels;
	while(averageSample(samples, playbackPos + centerSnap + length, channels) < 0)
		centerSnap += channels;

	playbackPos += centerSnap;

	sf::Vector2f samplePoints[length];
	for (uint16 i = 0; i < length; i++) {
		short sample = 0;
		uint32 samplePos = playbackPos + i * channels;
		if(samplePos < numSamples)
			sample = averageSample(samples, samplePos, channels);

		samplePoints[i] = sf::Vector2f(i * interval, verticalCenter - sample * invAmplitude);
	}

	//TODO: Expose this as a command line argument
	const float thickness = 4;
	std::vector<sf::Vertex> vertices;
	for(uint16 i = 1; i < length - 1; i++) {
		//TODO: Make sharp angles blunt to avoid "spikes"
		//TODO: This can probably be optimized
		sf::Vector2f pos = samplePoints[i];
		sf::Vector2f l1 = normalize(samplePoints[i + 1] - pos);
		sf::Vector2f l2 = normalize(pos - samplePoints[i - 1]);
		sf::Vector2f n1(-l1.y, l1.x);
		sf::Vector2f n2(-l2.y, l2.x);
		sf::Vector2f tangent = normalize(l1 + l2);
		sf::Vector2f miter(-tangent.y, tangent.x);
		float length = thickness;

		float d = dot(miter, n2);
		if(d != 0)
			length /= d;

		miter *= length;
		vertices.push_back(pos - miter);
		vertices.push_back(pos + miter);
	}

	target.draw(vertices.data(), vertices.size(), sf::TrianglesStrip);
}

short averageSample(const short* samples, const uint32 startIndex, const uint8 count) {
	short result = 0;
	for (uint32 i = startIndex; i < startIndex + count; i++) {
		result += samples[i];
	}

	return result / count;
}
