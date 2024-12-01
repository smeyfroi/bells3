#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "ofxSelfOrganizingMap.h"
#include "ofxAudioAnalysisClient.h"
#include "ofxAudioData.h"
#include "FluidSimulation.h"
#include "MaskShader.h"
#include "MultiplyColorShader.h"
#include "TranslateShader.h"
#include "LogisticFnShader.h"
#include "ofxIntrospector.h"
#include "Constants.h"
#include "ofxDividedArea.h"

using DkmClusterResults = std::tuple<std::vector<std::array<float, 2>>, std::vector<uint32_t>>; // (x,y),id

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

  void setupSom();

  void drawConnections();
  void updateRecentNotes(float s, float t, float u, float v);
  void updateClusters();
  void decayClusters();
  void updateSom(float x, float y, float z);
  void drawForegroundNoteMark(float x, float y, ofFloatColor color);
  void drawFluidNoteMark(float x, float y, ofFloatColor color);
  void drawForegroundClusterMarks(float x, float y, ofFloatColor color);
  void drawFluidClusterMarks(float x, float y, ofFloatColor color);
  ofFbo drawComposite();

  std::shared_ptr<ofxAudioAnalysisClient::FileClient> audioAnalysisClientPtr;
  std::shared_ptr<ofxAudioData::Processor> audioDataProcessorPtr;
  std::shared_ptr<ofxAudioData::Plots> audioDataPlotsPtr;
  std::shared_ptr<ofxAudioData::SpectrumPlots> audioDataSpectrumPlotsPtr;

  ofxSelfOrganizingMap som;
  ofFloatColor somColorAt(float x, float y) const;
  bool somVisible;
  ofImage somImage;

  MultiplyColorShader fadeShader;
  TranslateShader translateShader;
  LogisticFnShader logisticFnShader;

  FluidSimulation fluidSimulation;
  ofTexture frozenFluid;

  PingPongFbo foregroundFbo; // transient lines and circles
  
  PingPongFbo crystalFbo;
  ofFbo crystalMaskFbo;
  MaskShader maskShader;
  
  PingPongFbo divisionsFbo;
  DividedArea dividedArea { {1.0, 1.0}, 5 };

  std::vector<std::array<float, 2>> recentNoteXYs;
  DkmClusterResults clusterResults;
  std::vector<glm::vec4> clusterCentres;

  ofFbo compositeFbo;

  Introspector introspector; // add things to this in normalised coords
  
  bool guiVisible { false };
  ofxPanel gui;
  ofParameterGroup parameters;
  
  ofParameterGroup audioParameters { "audio" };
  ofParameter<float> validLowerRmsParameter { "validLowerRms", 150.0, 100.0, 5000.0 };
  ofParameter<float> validLowerPitchParameter { "validLowerPitch", 50.0, 50.0, 8000.0 };
  ofParameter<float> validUpperPitchParameter { "validUpperPitch", 5000.0, 50.0, 8000.0 };
  ofParameter<float> minPitchParameter { "minPitch", 150.0, 0.0, 8000.0 };
  ofParameter<float> maxPitchParameter { "maxPitch", 1500.0, 0.0, 8000.0 };
  ofParameter<float> minRMSParameter { "minRMS", 0.0, 0.0, 6000.0 };
  ofParameter<float> maxRMSParameter { "maxRMS", 1000.0, 0.0, 6000.0 };
  ofParameter<float> minSpectralKurtosisParameter { "minSpectralKurtosis", 0.0, 0.0, 6000.0 };
  ofParameter<float> maxSpectralKurtosisParameter { "maxSpectralKurtosis", 25.0, 0.0, 6000.0 };
  ofParameter<float> minSpectralCentroidParameter { "minCentroidKurtosis", 0.4, 0.0, 10.0 };
  ofParameter<float> maxSpectralCentroidParameter { "maxCentroidKurtosis", 6.0, 0.0, 10.0 };

  ofParameterGroup clusterParameters { "cluster" };
  ofParameter<int> clusterCentresParameter { "clusterCentres", 15, 2, 60 };
  ofParameter<int> clusterSourceSamplesMaxParameter { "clusterSourceSamplesMax", 4000, 1000, 8000 }; // Note: 1600 raw samples per frame at 30fps
  ofParameter<float> clusterDecayRateParameter { "clusterDecayRate", 0.98, 0.0, 1.0 };
  ofParameter<float> sameClusterToleranceParameter { "sameClusterTolerance", 0.2, 0.01, 1.0 };

  ofParameterGroup crystalParameters { "crystal" };
  ofParameter<int> sampleNotesParameter { "sampleNotes", 30, 5, 50 };

  ofParameterGroup fadeParameters { "fade" };
  ofParameter<float> fadeCrystalsParameter { "fadeCrystals", 0.9975, 0.9, 1.0 };
  ofParameter<float> fadeDivisionsParameter { "fadeDivisions", 0.93, 0.9, 1.0 };
  ofParameter<float> fadeForegroundParameter { "fadeForeground", 0.996, 0.9, 1.0 };
  
  ofParameterGroup impulseParameters { "impulse" };
  ofParameter<float> impulseRadiusParameter { "impulseRadius", 0.085, 0.01, 0.2 };
  ofParameter<float> impulseRadialVelocityParameter { "impulseRadialVelocity", 0.0005, 0.0001, 0.001 };

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
