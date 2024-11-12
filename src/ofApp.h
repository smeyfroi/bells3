#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "ofxSelfOrganizingMap.h"
#include "ofxAudioAnalysisClient.h"
#include "ofxAudioData.h"
#include "FluidSimulation.h"
#include "MaskShader.h"
#include "ofxIntrospector.h"
#include "ofxPlottable.h"
#include "Constants.h"
#include "ofxDividedArea.h"

class ofApp : public ofBaseApp{
  
public:
  void setup() override;
  void update() override;
  void draw() override;
  void exit() override;
  
  void keyPressed(int key) override;
  void keyReleased(int key) override;
  void mouseMoved(int x, int y ) override;
  void mouseDragged(int x, int y, int button) override;
  void mousePressed(int x, int y, int button) override;
  void mouseReleased(int x, int y, int button) override;
  void mouseScrolled(int x, int y, float scrollX, float scrollY) override;
  void mouseEntered(int x, int y) override;
  void mouseExited(int x, int y) override;
  void windowResized(int w, int h) override;
  void dragEvent(ofDragInfo dragInfo) override;
  void gotMessage(ofMessage msg) override;
  
private:

  // bells
//    std::shared_ptr<ofxAudioAnalysisClient::FileClient> audioAnalysisClientPtr {
//      std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240517-155805463/____-80_41_155_x_22141-0-1.wav",
//                                                           "Jam-20240517-155805463/____-80_41_155_x_22141.oscs") };

  // nightsong
//    std::shared_ptr<ofxAudioAnalysisClient::FileClient> audioAnalysisClientPtr {
//      std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240402-094851837/____-46_137_90_x_22141-0-1.wav",
//                                                           "Jam-20240402-094851837/____-46_137_90_x_22141.oscs") };

  // treganna
  std::shared_ptr<ofxAudioAnalysisClient::FileClient> audioAnalysisClientPtr {
    std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240719-093508910/____-92_9_186_x_22141-0-1.wav",
                                                         "Jam-20240719-093508910/____-92_9_186_x_22141.oscs") };

  std::shared_ptr<ofxAudioData::Processor> audioDataProcessorPtr { std::make_shared<ofxAudioData::Processor>(audioAnalysisClientPtr) };
  std::shared_ptr<ofxAudioData::Plots> audioDataPlotsPtr { std::make_shared<ofxAudioData::Plots>(audioDataProcessorPtr) };
  std::shared_ptr<ofxAudioData::SpectrumPlots> audioDataSpectrumPlotsPtr { std::make_shared<ofxAudioData::SpectrumPlots>(audioDataProcessorPtr) };
  
  ofxSelfOrganizingMap som;
  ofFloatColor somColorAt(float x, float y) const;
  
  FluidSimulation fluidSimulation;
  ofTexture frozenFluid;

  ofFbo foregroundFbo; // transient lines and circles
  
  ofFbo crystalFbo;
  ofFbo crystalMaskFbo;
  MaskShader maskShader;
  
  ofFbo divisionsFbo;
  DividedArea dividedArea { {1.0, 1.0}, 7 };

  std::vector<std::array<float, 2>> recentNoteXYs;
  std::tuple<std::vector<std::array<float, 2>>, std::vector<uint32_t>> clusterResults;
  std::vector<glm::vec4> clusterCentres;
  
  Plottable plot { Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT }; // We draw in normalised coords so scale up for drawing and saving into a window-shaped viewport
  
  Introspector introspector; // add things to this in normalised coords
  
  bool guiVisible { false };
  ofxPanel gui;
  ofParameterGroup parameters;
  
  ofParameterGroup audioParameters { "audio" };
  ofParameter<float> validLowerRmsParameter { "validLowerRms", 300.0, 100.0, 5000.0 };
  ofParameter<float> validLowerPitchParameter { "validLowerPitch", 50.0, 50.0, 8000.0 };
  ofParameter<float> validUpperPitchParameter { "validUpperPitch", 5000.0, 50.0, 8000.0 };
  ofParameter<float> minPitchParameter { "minPitch", 200.0, 0.0, 8000.0 };
  ofParameter<float> maxPitchParameter { "maxPitch", 1800.0, 0.0, 8000.0 };
  ofParameter<float> minRMSParameter { "minRMS", 0.0, 0.0, 6000.0 };
  ofParameter<float> maxRMSParameter { "maxRMS", 4600.0, 0.0, 6000.0 };
  ofParameter<float> minSpectralKurtosisParameter { "minSpectralKurtosis", 0.0, 0.0, 6000.0 };
  ofParameter<float> maxSpectralKurtosisParameter { "maxSpectralKurtosis", 25.0, 0.0, 6000.0 };
  ofParameter<float> minSpectralCentroidParameter { "minCentroidKurtosis", 0.4, 0.0, 10.0 };
  ofParameter<float> maxSpectralCentroidParameter { "maxCentroidKurtosis", 6.0, 0.0, 10.0 };

  ofParameterGroup clusterParameters { "cluster" };
  ofParameter<int> clusterCentresParameter { "clusterCentres", 12, 2.0, 50.0 };
  ofParameter<int> clusterSourceSamplesMaxParameter { "clusterSourceSamplesMax", 3000, 1000, 8000 }; // Note: 1600 raw samples per frame at 30fps
  ofParameter<float> clusterDecayRateParameter { "clusterDecayRate", 1.1, 0.0, 5.0 };
  ofParameter<float> sameClusterToleranceParameter { "sameClusterTolerance", 0.1, 0.01, 1.0 };
  ofParameter<int> sampleNoteClustersParameter { "sampleNoteClusters", 7, 1, 20 };
  ofParameter<int> sampleNotesParameter { "sampleNotes", 7, 1, 20 };

  ofParameterGroup fadeParameters { "fade" };
  ofParameter<float> fadeCrystalsParameter { "fadeCrystals", 0.01, 0.001, 0.1 };
  ofParameter<float> fadeDivisionsParameter { "fadeDivisions", 0.06, 0.001, 0.1 };
  ofParameter<float> fadeForegroundParameter { "fadeForeground", 0.005, 0.001, 0.1 };
  
  ofParameterGroup impulseParameters { "impulse" };
  ofParameter<float> impulseRadiusParameter { "impulseRadius", 0.085, 0.01, 0.2 };
  ofParameter<float> impulseRadialVelocityParameter { "impulseRadialVelocity", 0.0003, 0.0001, 0.001 };

  // draw extended outlines in the foreground (saving them for redrawing into fluid)
  //  float width = 15 * 1.0 / foregroundLinesFbo.getWidth();
  // redraw extended lines into the fluid layer
  //  float width = 1.0 * 1.0 / Constants::FLUID_WIDTH;
  // draw circles around longer-lasting clusterCentres into fluid layer
  //  if (p.w < 5.0) continue;
  //  ofDrawCircle(p.x * Constants::FLUID_WIDTH, p.y * Constants::FLUID_HEIGHT, u * 100.0);
  //  TS_START("update-divider");
  //  const float lineWidth = 1.0 * 1.0 / Constants::FLUID_WIDTH;
  //  TS_START("decay-clusterCentres");
  //  if (p.w > 5.0) {
  // draw divisions on foreground
  //  const float lineWidth = 80.0 * 1.0 / foregroundLinesFbo.getWidth();
  // draw arcs around longer-lasting clusterCentres into foreground
  //  if (p.w < 4.0) continue;
  //  float radius = std::fmod(p.w*5.0, 480);
  // ^^^ similar for plots
  
  // All the plot lifetimes
  
};
