#include "lib/tinyfiledialogs.h"
#include <SFML/OpenGL.hpp>
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <climits>
#include <cmath>
#include <sstream>
#include <functional>
#include <queue>

//Vector helper macros
#define vlength(a) (float)sqrt((a).x*(a).x + (a).y*(a).y)
#define vnormalize(a) (a) / vlength(a)
#define vdot(a,b) ((a).x*(b).x)+((a).y*(b).y)
#define vproject(a,b) (a)*(vdot(a,b)/(vlength(a)*vlength(a)))

//Math helper macros
#define msign(a) (a==0 ? 0 : (a>0 ? 1 : -1))
#define mclamp(a,b,c) ((a)<(b)?(b):((a)>(c)?(c):(a)))

//TODO: Screen resolution should not be constant and probably be exposed as a command line argument
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

void drawWaveform(sf::RenderTarget& target, sf::SoundBuffer& buffer, int playbackPos, const uint8 index, const uint8 count, const uint32 length);
short averageSample(const short* samples, const uint32 startIndex, const uint8 count, const uint32 length);
void encodeFrame(FILE* ffmpeg, sf::Mutex* mutex, std::queue<sf::Image>* buffer, bool* isDone);

int main(int argc, char* argv[])
{
	bool preview = true;
	//TODO: Add command line argument to change this
	int sampleLen = 1500;

	std::string outputFile = "output.mp4";
	std::vector<std::string> inputFiles;

	if(argc == 1) {
		char const * openFilters[] = { "*.wav" };
		char const * saveFilters[] = { "*.mp4" };
		const char* path = tinyfd_openFileDialog("Select master audio", NULL, 1, openFilters, "Wave-files", 0);
		if(path == NULL)
			return -1;
		
		inputFiles.push_back(path);
		
		path = tinyfd_openFileDialog("Select waveform channels", NULL, 1, openFilters, "Wave-files", 1);
		if(path == NULL)
			return -1;

		std::string paths = path;
		std::string delimiter = "|";
		size_t pos = 0;
		std::string token;
		while((pos = paths.find(delimiter)) != std::string::npos) {
			token = paths.substr(0, pos);
			inputFiles.push_back(token);
			paths.erase(0, pos + delimiter.length());
		}

		inputFiles.push_back(paths);

		//TODO: Set thickness/color(s)/other settings

		path = tinyfd_saveFileDialog("Set output file", NULL, 1, saveFilters, "MP4 video");
		if (path == NULL)
			return -1;

		std::string output = path;
		if (output.find(".mp4") == std::string::npos) {
			std::cout << "File ending missing!" << std::endl;
			output += ".mp4";
		}
			
		outputFile = output;

	} else {
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
	}

	std::vector<sf::SoundBuffer*> buffers;
	for (uint8 i = 0; i < inputFiles.size(); i++) {
		sf::SoundBuffer* buffer = new sf::SoundBuffer();
		std::cout << "Loading " << inputFiles[i] << "..." << std::endl;
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

	std::cout << "Trying to open window!" << std::endl;

	//TODO: Figure out a way to enable anti-aliasing without using a RenderWindow
	sf::ContextSettings settings(8, 8, 16);
	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Rendering...", sf::Style::Default, settings);
	sf::Texture capture;
	capture.create(WINDOW_WIDTH, WINDOW_HEIGHT);

	std::ostringstream cmd;
	cmd << 	"ffmpeg -r 60 -f rawvideo -pix_fmt rgba -s " << WINDOW_WIDTH << "x" << WINDOW_HEIGHT << " -i - -i " << inputFiles[0] << " -strict -2 " <<
			"-threads 0 -preset fast -y -pix_fmt yuv420p -crf 21 " << outputFile;
#if WIN32
	FILE* ffmpeg = _popen(cmd.str().c_str(), "w");
	if (!ffmpeg)
		std::cout << "Failed to open pipe!!!" << std::endl;
#else
	FILE* ffmpeg = popen(cmd.str().c_str(), "w");
#endif

	const uint16 samplesPerFrame = buffers[0]->getSampleRate() / 60;
	const uint32 numFrames = buffers[0]->getSampleCount() / buffers[0]->getChannelCount() / samplesPerFrame;
	std::cout << "Preparing to render " << numFrames << " frames" << std::endl;

	bool isDone = false;
	std::queue<sf::Image> framebuffer;
	sf::Mutex encodeMutex;
	sf::Thread encodeThread(std::bind(&encodeFrame, ffmpeg, &encodeMutex, &framebuffer, &isDone));
	encodeThread.launch();

	//TODO: Rendering could probably be even faster if we could render multiple frames on separate threads
	//		This requires that we get anti-aliasing working on RenderTextures
	//		Probably limited by FFmpeg encoding speed though
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
		encodeMutex.lock();
		framebuffer.push(img);
		encodeMutex.unlock();

		currentFrame++;
		int progress = ceil(((float)currentFrame / (float)numFrames) * 10000) / 100;
		std::ostringstream title;
		title << "oscigen (" << progress << "%)";
		window.setTitle(title.str());
	}

	isDone = true;
	encodeThread.wait();
#if WIN32
	_pclose(ffmpeg);
#else
	pclose(ffmpeg);
#endif
	window.close();

	return 0;
}

void encodeFrame(FILE* ffmpeg, sf::Mutex* mutex, std::queue<sf::Image>* buffer, bool* isDone) {
	while(!*isDone) {
		mutex->lock();
		if(buffer->size() > 0) {
			fwrite(buffer->front().getPixelsPtr(), WINDOW_WIDTH*WINDOW_HEIGHT*4, 1, ffmpeg);
			buffer->pop();
		}
		mutex->unlock();
	}
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
	while(averageSample(samples, playbackPos + centerSnap + length, channels, numSamples) > 0)
		centerSnap += channels;
	while(averageSample(samples, playbackPos + centerSnap + length, channels, numSamples) < 0)
		centerSnap += channels;

	playbackPos += centerSnap;

	const int averageCount = 2;
	std::vector<sf::Vector2f> points;
	for (uint16 i = 0; i < length; i+=2) {
		int ampAverage = 0;

		for (uint8 s = 0; s < averageCount; s++) {
			uint32 samplePos = playbackPos + (i + s) * channels;
			ampAverage += averageSample(samples, samplePos, channels, numSamples);
		}

		ampAverage /= averageCount;
		sf::Vector2f pos = sf::Vector2f(i * interval, verticalCenter - ampAverage * invAmplitude);
		points.push_back(pos);
	}

	std::vector<sf::Vertex> triVerts;
	sf::Vector2f prevPos = points[0];
	sf::Vector2f currPos = points[1];

	//TODO: Expose this as a command line argument
	const float thickness = 4;
	glLineWidth(thickness);
	//TODO: Expose this as well?
	const int maxSegments = 8;

	for(auto it = points.begin() + 1; it != points.end() - 1; it++) {
		//TODO: This can probably be optimized even further
		sf::Vector2f nextPos = *(it + 1);

		sf::Vector2f l1 = vnormalize(nextPos - currPos);
		sf::Vector2f n1(l1.y, -l1.x);
		sf::Vector2f l2 = vnormalize(currPos - prevPos);
		sf::Vector2f n2(l2.y, -l2.x);

		if(vdot(n1, l2) < 0) {
			n1 = -n1;
			n2 = -n2;
		}

		int segments = ceil((1 - (1 + vdot(n1, n2)) * 0.5) * maxSegments);
		float invSegment = 1.0f / segments;
		sf::Vector2f s1;
		sf::Vector2f s2 = n1;
		s2 = vnormalize(s2);

		for(int i = 1; i <= segments; i++) {
			float s = invSegment * i;
			s1 = s2;
			s2 = n1 * (1 - s) + n2 * s;
			s2 = vnormalize(s2);

			triVerts.push_back(currPos);
			triVerts.push_back(currPos + s1 * thickness * 0.5f);
			triVerts.push_back(currPos + s2 * thickness * 0.5f);
		}

		n2 *= thickness * 0.5f;
		triVerts.push_back(prevPos + n2);
		triVerts.push_back(prevPos - n2);
		triVerts.push_back(currPos + n2);

		triVerts.push_back(prevPos - n2);
		triVerts.push_back(currPos + n2);
		triVerts.push_back(currPos - n2);

		prevPos = currPos;
		currPos = nextPos;
	}

	target.draw(triVerts.data(), triVerts.size(), sf::Triangles);
}

short averageSample(const short* samples, const uint32 startIndex, const uint8 count, const uint32 length) {
	if(startIndex + count >= length)
		return 0;

	short result = 0;
	for (uint32 i = startIndex; i < startIndex + count; i++) {
		result += samples[i];
	}

	return result / count;
}
