#include "ofMain.h"
#include "ofApp.h"
#include "Constants.h"

//========================================================================
int main( ){

	//Use ofGLFWWindowSettings for more options like multi-monitor fullscreen
	ofGLWindowSettings settings;
  settings.setSize(Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
	settings.windowMode = OF_WINDOW; //can also be OF_FULLSCREEN

	auto window = ofCreateWindow(settings);

	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();

}
