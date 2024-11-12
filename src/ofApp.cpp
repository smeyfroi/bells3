#include "ofApp.h"
#include "ofxTimeMeasurements.h"
#include "dkm.hpp"

const int DEFAULT_CIRCLE_RESOLUTION = 32;
const int FOREGROUND_CIRCLE_RESOLUTION = 96;

//--------------------------------------------------------------
void ofApp::setup(){
  ofSetVerticalSync(false);
  ofEnableAlphaBlending();
  ofDisableArbTex(); // required for texture2D to work in GLSL, makes texture coords normalized
  ofSetFrameRate(Constants::FRAME_RATE);
  ofSetCircleResolution(DEFAULT_CIRCLE_RESOLUTION);
  TIME_SAMPLE_SET_FRAMERATE(Constants::FRAME_RATE);

  double minInstance[3] = { 0.0, 0.0, 0.0 };
  double maxInstance[3] = { 1.0, 1.0, 1.0 };
  som.setFeaturesRange(3, minInstance, maxInstance);
  som.setMapSize(Constants::SOM_WIDTH, Constants::SOM_HEIGHT); // can go to 3 dimensions
  som.setInitialLearningRate(0.1);
  som.setNumIterations(3000);
  som.setup();
  
  fluidSimulation.setup({ Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT });
  
  divisionsFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA); // GL_RGBA32F); // 8 bit for ghosts of past lines
  divisionsFbo.clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));
  
  foregroundFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA32F);
  foregroundFbo.clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));
  
  crystalFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA);
  crystalFbo.clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));
  crystalMaskFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_R8);
  maskShader.load();
  
  audioParameters.add(validLowerRmsParameter);
  audioParameters.add(validLowerPitchParameter);
  audioParameters.add(validUpperPitchParameter);
  audioParameters.add(minPitchParameter);
  audioParameters.add(maxPitchParameter);
  audioParameters.add(minRMSParameter);
  audioParameters.add(maxRMSParameter);
  audioParameters.add(minSpectralKurtosisParameter);
  audioParameters.add(maxSpectralKurtosisParameter);
  audioParameters.add(minSpectralCentroidParameter);
  audioParameters.add(maxSpectralCentroidParameter);
  parameters.add(audioParameters);

  clusterParameters.add(clusterCentresParameter);
  clusterParameters.add(clusterSourceSamplesMaxParameter);
  clusterParameters.add(clusterDecayRateParameter);
  clusterParameters.add(sameClusterToleranceParameter);
  clusterParameters.add(sampleNoteClustersParameter);
  clusterParameters.add(sampleNotesParameter);
  parameters.add(clusterParameters);
  
  fadeParameters.add(fadeCrystalsParameter);
  fadeParameters.add(fadeDivisionsParameter);
  fadeParameters.add(fadeForegroundParameter);
  parameters.add(fadeParameters);
  
  impulseParameters.add(impulseRadiusParameter);
  impulseParameters.add(impulseRadialVelocityParameter);
  parameters.add(impulseParameters);

  auto fluidParameterGroup = fluidSimulation.getParameterGroup();
  fluidParameterGroup.getFloat("dt").set(0.02);
  fluidParameterGroup.getFloat("vorticity").set(15.0);
  fluidParameterGroup.getFloat("value:dissipation").set(0.9975);
  fluidParameterGroup.getFloat("velocity:dissipation").set(0.9999);
  fluidParameterGroup.getInt("pressure:iterations").set(22);
  parameters.add(fluidParameterGroup);
  
  gui.setup(parameters);

  ofxTimeMeasurements::instance()->setEnabled(false);
}

//--------------------------------------------------------------
void ofApp::update() {
  TS_START("update-introspection");
  introspector.update();
  TS_STOP("update-introspection");

  TS_START("update-audoanalysis");
  audioDataProcessorPtr->update();
  TS_STOP("update-audoanalysis");

  // fade crystals
  crystalFbo.begin();
  ofEnableBlendMode(OF_BLENDMODE_ALPHA);
  ofSetColor(ofFloatColor(0.0, 0.0, 0.0, fadeCrystalsParameter));
  ofDrawRectangle(0.0, 0.0, crystalFbo.getWidth(), crystalFbo.getHeight());
  crystalFbo.end();

  // fade division lines
  divisionsFbo.begin();
  ofEnableBlendMode(OF_BLENDMODE_ALPHA);
  ofSetColor(ofFloatColor(0.0, 0.0, 0.0, fadeDivisionsParameter));
  ofDrawRectangle(0.0, 0.0, divisionsFbo.getWidth(), divisionsFbo.getHeight());
  divisionsFbo.end();

  // fade foreground
  foregroundFbo.begin();
  ofEnableBlendMode(OF_BLENDMODE_ALPHA);
  ofSetColor(ofFloatColor(0.0, 0.0, 0.0, fadeForegroundParameter));
  ofDrawRectangle(0.0, 0.0, foregroundFbo.getWidth(), foregroundFbo.getHeight());
  foregroundFbo.end();

  float s = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::pitch, minPitchParameter, maxPitchParameter);// 700.0, 1300.0);
  float t = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, minRMSParameter, maxRMSParameter); ////400.0, 4000.0, false);
  float u = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralKurtosis, minSpectralKurtosisParameter, maxSpectralKurtosisParameter);
  float v = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralCentroid, minSpectralCentroidParameter, maxSpectralCentroidParameter);
  
  std::vector<ofxAudioData::ValiditySpec> sampleValiditySpecs {
    {ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, false, validLowerRmsParameter},
    {ofxAudioAnalysisClient::AnalysisScalar::pitch, false, validLowerPitchParameter},
    {ofxAudioAnalysisClient::AnalysisScalar::pitch, true, validUpperPitchParameter}
  };

  if (audioDataProcessorPtr->isDataValid(sampleValiditySpecs)) {
    TS_START("update-som");
    {
      double instance[3] = { static_cast<double>(s), static_cast<double>(t), static_cast<double>(v) };
      som.updateMap(instance);
    }
    TS_STOP("update-som");

    ofFloatColor somColor = somColorAt(s, t);
    ofFloatColor darkSomColor = somColor; darkSomColor.setBrightness(0.25); darkSomColor.setSaturation(1.0);

    // Draw foreground mark for raw audio data sample in darkened SOM color
    foregroundFbo.begin();
    {
      ofEnableBlendMode(OF_BLENDMODE_DISABLED);
      ofSetColor(darkSomColor);
      ofDrawCircle(s*foregroundFbo.getWidth(), t*foregroundFbo.getHeight(), 10.0);
    }
    foregroundFbo.end();

    // Draw fluid mark for raw audio data sample in darkened SOM color
    fluidSimulation.getFlowValuesFbo().getSource().begin();
    {
      ofEnableBlendMode(OF_BLENDMODE_DISABLED);
      ofSetColor(darkSomColor);
      ofDrawCircle(s*Constants::FLUID_WIDTH, t*Constants::FLUID_HEIGHT, 3.0);
    }
    fluidSimulation.getFlowValuesFbo().getSource().end();

    // Maintain recent notes
    if (recentNoteXYs.size() > clusterSourceSamplesMaxParameter) {
      recentNoteXYs.erase(recentNoteXYs.end() - clusterSourceSamplesMaxParameter/10, recentNoteXYs.end());
    }
    recentNoteXYs.push_back({ s, t });
    introspector.addCircle(s, t, 1.0/Constants::WINDOW_WIDTH*5.0, ofColor::yellow, true, 30); // introspection: small yellow circle for new raw source sample

    TS_START("update-kmeans");
    if (recentNoteXYs.size() > clusterCentresParameter) {
      dkm::clustering_parameters<float> params(clusterCentresParameter);
      params.set_random_seed(1000); // keep clusters stable
      clusterResults = dkm::kmeans_lloyd(recentNoteXYs, params);
    }
    TS_STOP("update-kmeans");
    
    TS_START("update-clusterCentres");
    {
      // glm::vec4 w is age
      // add to clusterCentres from new clusters
      for (const auto& cluster : std::get<0>(clusterResults)) {
        float x = cluster[0]; float y = cluster[1];
        auto it = std::find_if(clusterCentres.begin(),
                               clusterCentres.end(),
                               [x, y, this](const glm::vec4& p) {
          return ((std::abs(p.x-x) < sameClusterToleranceParameter) && (std::abs(p.y-y) < sameClusterToleranceParameter));
        });
        if (it == clusterCentres.end()) {
          // don't have this clusterCentre so make it
          clusterCentres.push_back(glm::vec4(x, y, 0.0, 1.0)); // start at age=1
          introspector.addCircle(x, y, 20.0*1.0/Constants::WINDOW_WIDTH, ofColor::red, true, 100); // introspection: large red circle is new cluster centre
        } else {
          // existing cluster so add to its age to preserve it
          it->w++;
//          introspection.addCircle(it->x, it->y, 3.0*1.0/Constants::WINDOW_WIDTH, ofColor::yellow, false, 10); // introspection: small yellow circle is existing cluster centre that continues to exist
        }
      }
    }
    TS_STOP("update-clusterCentres");
    
    // Make fine structure from some recent notes
    const std::vector<uint32_t>& recentNoteXYIds = std::get<1>(clusterResults);
    if (recentNoteXYs.size() > 70) {
      
      // find some number of note clusters
      for (int i = 0; i < sampleNoteClustersParameter; i++) {
        
        std::vector<uint32_t> sameClusterNoteIds; // collect note IDs all from the same cluster
        size_t id = ofRandom(recentNoteXYIds.size()); // start with a random note TODO: don't use ofRandom
        sameClusterNoteIds.push_back(id);
        uint32_t clusterId = recentNoteXYIds[id];
        
        // pick a number of additional random notes and keep if from this cluster
        for(int i = 0; i < sampleNotesParameter; i++) {
          id = ofRandom(recentNoteXYIds.size());
          if (recentNoteXYIds[id] == clusterId) {
            sameClusterNoteIds.push_back(id);
          }
        }

        // if we found enough related notes then draw something
        if (sameClusterNoteIds.size() > 2) {
//          ofLogNotice() << sameClusterNoteIds.size();
          
          // make path from notes in normalised coords
          ofPath path;
          for (uint32_t id : sameClusterNoteIds) {
            const auto& note = recentNoteXYs[id];
            path.lineTo(note[0], note[1]);
          }
          path.close();

          // find normalised path bounds
          ofRectangle pathBounds;
          for (const auto& polyline : path.getOutline()) {
            pathBounds = pathBounds.getUnion(polyline.getBoundingBox());
          }
          
          // scale up to some limit to fill mask with a reduced view of some part of the frozen fluid
          constexpr float MAX_SCALE = 2.0;
          float scaleX = std::fminf(MAX_SCALE, 1.0 / pathBounds.width);
          float scaleY = std::fminf(MAX_SCALE, 1.0 / pathBounds.height);
          float scale = std::fminf(scaleX, scaleY);

          // paint path into the fluid layer
          fluidSimulation.getFlowValuesFbo().getSource().begin();
          {
            ofPath fluidPath = path;
            fluidPath.scale(Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT);
            ofEnableBlendMode(OF_BLENDMODE_ALPHA);
            ofFloatColor fillColor = somColor;
            fillColor.a = 0.3;
            fluidPath.setColor(fillColor);
            fluidPath.setFilled(true);
            fluidPath.draw();
          }
          fluidSimulation.getFlowValuesFbo().getSource().end();
          
          if (frozenFluid.isAllocated()) {

            // make a mask texture
            crystalMaskFbo.begin();
            {
              ofPath maskPath = path;
              ofEnableBlendMode(OF_BLENDMODE_DISABLED);
              ofClear(0, 255);
              ofSetColor(255);
              maskPath.setFilled(true);
              maskPath.scale(crystalMaskFbo.getWidth(), crystalMaskFbo.getHeight());
              maskPath.draw();
            }
            crystalMaskFbo.end();
            
            // draw a reduced SOM-tinted version of the frozen fluid into the crystal layer through the mask
            crystalFbo.begin();
            {
              ofEnableBlendMode(OF_BLENDMODE_ADD);
//              ofFloatColor fragmentColor = somColorAt(pathBounds.x, pathBounds.y);
//              fragmentColor.a = 0.2;
//              ofSetColor(fragmentColor*0.2);
              ofSetColor(128);
              maskShader.render(frozenFluid, crystalMaskFbo, crystalFbo.getWidth(), crystalFbo.getHeight(), false, {pathBounds.x+pathBounds.width/2.0, pathBounds.y+pathBounds.height/2.0}, {scale, scale});
            }
            crystalFbo.end();
          }
          
          // draw extended outlines in the foreground (saving them for redrawing into fluid)
          std::vector<DividerLine> extendedLines;
          float width = 8 * 1.0 / divisionsFbo.getWidth();
          divisionsFbo.begin();
          ofEnableBlendMode(OF_BLENDMODE_ALPHA);
          ofSetColor(ofColor::black);
          ofPushMatrix();
          ofScale(foregroundFbo.getWidth(), foregroundFbo.getHeight());
          {
            for(auto iter = sameClusterNoteIds.begin(); iter < sameClusterNoteIds.end(); iter++) {
              auto id1 = *iter;
              const auto& note1 = recentNoteXYs[id1];
              float x1 = note1[0]; float y1 = note1[1];
              uint32_t id2;
              if (iter == sameClusterNoteIds.end() - 1) {
                id2 = *sameClusterNoteIds.begin();
              } else {
                id2 = *(iter + 1);
              }
              const auto& note2 = recentNoteXYs[id2];
              float x2 = note2[0]; float y2 = note2[1];
              if (note1 == note2) continue;
              DividerLine line = dividedArea.createConstrainedDividerLine({x1, y1}, {x2, y2});
              extendedLines.push_back(line);
              glm::vec2 p1 = line.start; glm::vec2 p2 = line.end;
              ofPushMatrix();
              ofTranslate(p1.x, p1.y);
              ofRotateRad(std::atan2((p2.y-p1.y), (p2.x-p1.x)));
              ofDrawRectangle(0.0, -width/2.0, ofDist(p1.x, p1.y, p2.x, p2.y), width);
              ofPopMatrix();
            }
          }
          ofPopMatrix();
          divisionsFbo.end();
          
          // plot connected clustered notes
          {
            uint32_t lastNoteId = *(sameClusterNoteIds.end() - 1);
            auto lastNote = recentNoteXYs[lastNoteId];
            for (uint32_t id : sameClusterNoteIds) {
              const auto& note = recentNoteXYs[id];
              plot.addLine(lastNote[0], lastNote[1], note[0], note[1], ofColor::red, 50);
              lastNote = note;
            }
          }

          // plot extended lines
          {
            for (const auto& line : extendedLines) {
              glm::vec2 p1 = line.start; glm::vec2 p2 = line.end;
              plot.addLine(p1.x, p1.y, p2.x, p2.y, ofColor::green, 20);
            }
          }
          
          // redraw extended lines into the fluid layer
          fluidSimulation.getFlowValuesFbo().getSource().begin();
          ofEnableBlendMode(OF_BLENDMODE_ALPHA);
//          ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 0.7));
          ofSetColor(ofFloatColor(0.0, 0.0, 0.0, 0.3));
          ofPushMatrix();
          ofScale(Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT);
          {
            float width = 1.0 * 1.0 / Constants::FLUID_WIDTH;
            for (const auto& line : extendedLines) {
              glm::vec2 p1 = line.start; glm::vec2 p2 = line.end;
              ofPushMatrix();
              ofTranslate(p1.x, p1.y);
              ofRotateRad(std::atan2((p2.y-p1.y), (p2.x-p1.x)));
              ofDrawRectangle(0.0, -width/2.0, ofDist(p1.x, p1.y, p2.x, p2.y), width);
              ofPopMatrix();
            }
          }
          ofPopMatrix();
          fluidSimulation.getFlowValuesFbo().getSource().end();
        }
      }
    }

    // draw circles around longer-lasting clusterCentres into fluid layer
    fluidSimulation.getFlowValuesFbo().getSource().begin();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    ofNoFill();
    ofSetColor(ofFloatColor(0.1, 0.1, 0.1, 0.6));
    for (auto& p: clusterCentres) {
      if (p.w < 5.0) continue;
      ofDrawCircle(p.x * Constants::FLUID_WIDTH, p.y * Constants::FLUID_HEIGHT, u * 100.0);
    }
    fluidSimulation.getFlowValuesFbo().getSource().end();

    TS_START("update-divider");
    if (clusterCentres.size() > 2) {
      bool dividedAreaChanged = dividedArea.updateUnconstrainedDividerLines(clusterCentres, {(size_t)ofRandom(clusterCentres.size()), (size_t)ofRandom(clusterCentres.size())});
      if (dividedAreaChanged) {
        fluidSimulation.getFlowValuesFbo().getSource().begin();
        {
          ofEnableBlendMode(OF_BLENDMODE_ALPHA);
          ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 0.7));
          ofPushMatrix();
          ofScale(Constants::FLUID_WIDTH);
          const float lineWidth = 0.5 * 1.0 / Constants::FLUID_WIDTH;
          dividedArea.draw(0.0, lineWidth, 0.0);
          ofPopMatrix();
        }
        fluidSimulation.getFlowValuesFbo().getSource().end();
        
        ofPixels frozenPixels;
        fluidSimulation.getFlowValuesFbo().getSource().getTexture().readToPixels(frozenPixels);
        frozenFluid.allocate(frozenPixels);
      }
    }
    TS_STOP("update-divider");
    
  } //isDataValid()

  {
    TS_START("decay-clusterCentres");
    // age all clusterCentres
    for (auto& p: clusterCentres) {
      p.w -= clusterDecayRateParameter;
      if (p.w > 5.0) {
        introspector.addCircle(p.x, p.y, 10.0*1.0/Constants::WINDOW_WIDTH, ofColor::lightGreen, true, 60); // large lightGreen circle is long-lived clusterCentre
      } else {
        introspector.addCircle(p.x, p.y, 6.0*1.0/Constants::WINDOW_WIDTH, ofColor::darkOrange, true, 30); // small darkOrange circle is short-lived clusterCentre
      }
    }
    // delete decayed clusterCentres
    clusterCentres.erase(std::remove_if(clusterCentres.begin(),
                                        clusterCentres.end(),
                                        [](const glm::vec4& n) { return n.w <=0; }),
                         clusterCentres.end());
    TS_STOP("decay-clusterCentres");
  }
  
  // draw divisions on foreground
  {
    divisionsFbo.begin();
    ofSetColor(ofFloatColor(0.0, 0.0, 0.0, 1.0));
    ofPushMatrix();
    ofScale(divisionsFbo.getWidth(), divisionsFbo.getHeight());
    const float lineWidth = 80.0 * 1.0 / divisionsFbo.getWidth();
    dividedArea.draw(0.0, lineWidth, 0.0);
    ofPopMatrix();
    divisionsFbo.end();
  }

  // draw arcs around longer-lasting clusterCentres into foreground
  {
    foregroundFbo.begin();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofNoFill();
    for (auto& p: clusterCentres) {
      if (p.w < 4.0) continue;
      ofFloatColor somColor = somColorAt(p.x, p.y);
      ofFloatColor darkSomColor = somColor; darkSomColor.setBrightness(0.7); darkSomColor.setSaturation(1.0);
      darkSomColor.a = 0.7;
      ofSetColor(darkSomColor);
      ofPolyline path;
      float radius = std::fmod(p.w*5.0, 480);
      path.arc(p.x*foregroundFbo.getWidth(), p.y*foregroundFbo.getHeight(), radius, radius, -180.0*(u+p.x), 180.0*(v+p.y), FOREGROUND_CIRCLE_RESOLUTION);
      path.draw();
    }
    foregroundFbo.end();
  }
  
  // plot arcs around longer-lasting clusterCentres
  {
    for (auto& p: clusterCentres) {
      if (p.w < 4.0) continue;
      float radius = std::fmod(p.w*5.0/Constants::CANVAS_WIDTH, 480.0/Constants::CANVAS_WIDTH);
      plot.addArc(p.x, p.y, radius, -180.0*(u+p.x), 180.0*(v+p.y), ofColor::blue, 30);
    }
  }
  
  // plot divisions
  {
    for(auto& l : dividedArea.unconstrainedDividerLines) {
      plot.addLine(l.start.x, l.start.y, l.end.x, l.end.y, ofColor::black, 10);
    }
  }

  plot.update();

  {
    TS_START("update-fluid-clusters");
    auto& clusterCentres = std::get<0>(clusterResults);
    for (auto& centre : clusterCentres) {
      float x = centre[0]; float y = centre[1];
      const float COL_FACTOR = 0.008;
      ofFloatColor color = somColorAt(x, y) * COL_FACTOR;
      color.a = 0.005 * ofRandom(1.0);
      FluidSimulation::Impulse impulse {
        { x * Constants::FLUID_WIDTH, y * Constants::FLUID_HEIGHT },
        Constants::FLUID_WIDTH * impulseRadiusParameter,
        { 0.0, 0.0 }, // velocity
        impulseRadialVelocityParameter,
        color,
        1.0 // temperature
      };
      fluidSimulation.applyImpulse(impulse);
    }
    fluidSimulation.update();
    TS_STOP("update-fluid-clusters");
  }
}

ofFloatColor ofApp::somColorAt(float x, float y) const {
  double* somValue = som.getMapAt(x * Constants::SOM_WIDTH, y * Constants::SOM_HEIGHT);
  return ofFloatColor(somValue[0], somValue[1], somValue[2], 1.0);
}

//--------------------------------------------------------------
void ofApp::draw() {
  if (plot.visible) {
    ofClear(255, 255);
    plot.draw();
    
  } else {
    ofPushStyle();
    
//    ofClear(0, 255);
    
    // fluid
    {
      ofEnableBlendMode(OF_BLENDMODE_DISABLED);
      ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
      fluidSimulation.getFlowValuesFbo().getSource().draw(0.0, 0.0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
    }
    
    // foreground
    {
      ofEnableBlendMode(OF_BLENDMODE_ALPHA);
      ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
      foregroundFbo.draw(0, 0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
    }
    
    // divisions
    {
      ofEnableBlendMode(OF_BLENDMODE_ALPHA);
      ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
      divisionsFbo.draw(0, 0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
    }
    
    // crystals
    {
      ofEnableBlendMode(OF_BLENDMODE_ADD);
      ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
      crystalFbo.draw(0, 0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
    }

    ofPopStyle();
  }
  
  // introspection
  {
    TS_START("draw-introspection");
    ofPushStyle();
    ofPushView();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofScale(Constants::WINDOW_WIDTH); // drawing on Introspection is normalised so scale up
    introspector.draw();
    ofPopView();
    ofPopStyle();
    TS_STOP("draw-introspection");
  }
  
  // audio analysis graphs
  {
    ofPushStyle();
    ofPushView();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    float plotHeight = ofGetWindowHeight() / 4.0;
    audioDataPlotsPtr->drawPlots(ofGetWindowWidth(), plotHeight);
    audioDataSpectrumPlotsPtr->draw();
    ofPopView();
    ofPopStyle();
  }

  // gui
  if (guiVisible) gui.draw();
}

//--------------------------------------------------------------
void ofApp::exit(){
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
  if (audioAnalysisClientPtr->keyPressed(key)) return;
  if (key == OF_KEY_TAB) guiVisible = not guiVisible;
  {
    float plotHeight = ofGetWindowHeight() / 4.0;
    int plotIndex = ofGetMouseY() / plotHeight;
    bool plotKeyPressed = audioDataPlotsPtr->keyPressed(key, plotIndex);
    bool spectrumPlotKeyPressed = audioDataSpectrumPlotsPtr->keyPressed(key);
    if (plotKeyPressed || spectrumPlotKeyPressed) return;
  }
  if (introspector.keyPressed(key)) return;
  if (plot.keyPressed(key)) return;
  if (key == 'S') {
    ofFbo compositeFbo;
    compositeFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGB);
    compositeFbo.begin();
    {
      // blackground
//      ofClear(0, 255);

      // fluid
      {
        ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
        fluidSimulation.getFlowValuesFbo().getSource().draw(0.0, 0.0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
      }
      
      // foreground
      {
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
        foregroundFbo.draw(0, 0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
      }
      
      // divisions
      {
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
        divisionsFbo.draw(0, 0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
      }
      
      // crystals
      {
        ofEnableBlendMode(OF_BLENDMODE_ADD);
        ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
        crystalFbo.draw(0, 0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
      }
    }
    compositeFbo.end();
    ofPixels pixels;
    compositeFbo.readToPixels(pixels);
    ofSaveImage(pixels, ofFilePath::getUserHomeDir()+"/Documents/bells2/snapshot-"+ofGetTimestampString()+".png", OF_IMAGE_QUALITY_BEST);
  }
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
}

//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY){
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){
}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
}
