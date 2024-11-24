#include "ofApp.h"
#include "ofxTimeMeasurements.h"
#include "dkm.hpp"

//--------------------------------------------------------------
void ofApp::setupSom() {
  ofSetRandomSeed(1000); // keep SOM stable
  double minInstance[3] = { 0.0, 0.0, 0.0 };
  double maxInstance[3] = { 1.0, 1.0, 1.0 };
  som.setFeaturesRange(3, minInstance, maxInstance);
  som.setMapSize(Constants::SOM_WIDTH, Constants::SOM_HEIGHT); // can go to 3 dimensions
  som.setInitialLearningRate(0.1);
  som.setNumIterations(3000);
  som.setup();
}

void ofApp::setup(){
  ofSetVerticalSync(false);
  ofEnableAlphaBlending();
  ofDisableArbTex();
  ofSetCircleResolution(Constants::CIRCLE_RESOLUTION);
  ofSetFrameRate(Constants::FRAME_RATE);
  TIME_SAMPLE_SET_FRAMERATE(Constants::FRAME_RATE);

  // nightsong
  audioAnalysisClientPtr = std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240402-094851837/____-46_137_90_x_22141-0-1.wav", "Jam-20240402-094851837/____-46_137_90_x_22141.oscs");
  // bells
//  audioAnalysisClientPtr = std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240517-155805463/____-80_41_155_x_22141-0-1.wav", "Jam-20240517-155805463/____-80_41_155_x_22141.oscs");
  // treganna
//  audioAnalysisClientPtr = std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240719-093508910/____-92_9_186_x_22141-0-1.wav", "Jam-20240719-093508910/____-92_9_186_x_22141.oscs");
  
  audioDataProcessorPtr = std::make_shared<ofxAudioData::Processor>(audioAnalysisClientPtr);
  audioDataPlotsPtr = std::make_shared<ofxAudioData::Plots>(audioDataProcessorPtr);
  audioDataSpectrumPlotsPtr = std::make_shared<ofxAudioData::SpectrumPlots>(audioDataProcessorPtr);
  
  setupSom();
  
  fadeShader.load();
  translateShader.load();
  logisticFnShader.load();

  fluidSimulation.setup({ Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT });
  
  divisionsFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA); // GL_RGBA32F); // 8 bit for ghosts of past lines
  divisionsFbo.getSource().clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));
  
  foregroundFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA32F);
  foregroundFbo.getSource().clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));
  
  crystalFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA32F);
  crystalFbo.getSource().clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));
  crystalMaskFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_R8);
  maskShader.load();
  
  compositeFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGB);

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
  parameters.add(clusterParameters);
  
  crystalParameters.add(sampleNotesParameter);
  parameters.add(crystalParameters);
  
  fadeParameters.add(fadeCrystalsParameter);
  fadeParameters.add(fadeDivisionsParameter);
  fadeParameters.add(fadeForegroundParameter);
  parameters.add(fadeParameters);
  
  impulseParameters.add(impulseRadiusParameter);
  impulseParameters.add(impulseRadialVelocityParameter);
  parameters.add(impulseParameters);

  auto fluidParameterGroup = fluidSimulation.getParameterGroup();
  fluidParameterGroup.getFloat("dt").set(0.02);
  fluidParameterGroup.getFloat("vorticity").set(20.0);
  fluidParameterGroup.getFloat("value:dissipation").set(0.999);
  fluidParameterGroup.getFloat("velocity:dissipation").set(0.9999);
  fluidParameterGroup.getInt("pressure:iterations").set(20);
  parameters.add(fluidParameterGroup);
  
  gui.setup(parameters);

  ofxTimeMeasurements::instance()->setEnabled(false);
}

//--------------------------------------------------------------
void ofApp::updateRecentNotes(float s, float t, float u, float v) {
  TS_START("update-recent-notes");
  if (recentNoteXYs.size() > clusterSourceSamplesMaxParameter) {
    // erase oldest 10% of the max
    recentNoteXYs.erase(recentNoteXYs.begin(), recentNoteXYs.begin() + clusterSourceSamplesMaxParameter/10);
  }
  recentNoteXYs.push_back({ s, t });
  introspector.addCircle(s, t, 1.0/Constants::WINDOW_WIDTH*5.0, ofColor::yellow, true, 30); // introspection: small yellow circle for new raw source sample
  fluidSimulation.getFlowValuesFbo().getSource().begin();
  ofEnableBlendMode(OF_BLENDMODE_ADD);
//  ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0) * 0.04);
  float lastX = -1.0; float lastY = -1.0;
  for (const auto [x, y] : recentNoteXYs) {
    if (lastX > -1.0) {
      ofFloatColor color = somColorAt(x, y); color.setBrightness(0.8); color.setSaturation(1.0);
      ofSetColor(color * 0.001);
      ofDrawLine(lastX, lastY, x, y);
      lastX = x; lastY = y;
    }
  }
  fluidSimulation.getFlowValuesFbo().getSource().end();
  TS_STOP("update-recent-notes");
}

void ofApp::updateClusters() {
  if (recentNoteXYs.size() <= clusterCentresParameter) return;

  TS_START("update-kmeans");
  {
    dkm::clustering_parameters<float> params { static_cast<uint32_t>(clusterCentresParameter) };
    params.set_random_seed(1000); // keep clusters stable
    clusterResults = dkm::kmeans_lloyd(recentNoteXYs, params);
  }
  TS_STOP("update-kmeans");
  
  TS_START("update-clusterCentres");
  {
    // glm::vec4 w is age
    // add to clusterCentres from new clusters
    for (const auto& cluster : std::get<0>(clusterResults)) {
      float x = cluster[0]; float y = cluster[1]; // replacing with a structured binding here requires c++20 for the lambda capture below
      // find a similar existing cluster
      auto it = std::find_if(clusterCentres.begin(),
                             clusterCentres.end(),
                             [x, y, this](const glm::vec4& p) {
        return (glm::distance2(static_cast<glm::vec2>(p), {x, y}) < sameClusterToleranceParameter);
      });
      if (it == clusterCentres.end()) {
        // don't have this clusterCentre so make it
        clusterCentres.push_back({ x, y, 0.0, 1.0 }); // start at age=1
        introspector.addCircle(x, y, 15.0*1.0/Constants::WINDOW_WIDTH, ofColor::red, true, 10); // introspection: small red circle is new cluster centre
      } else {
        // close to an existing one, so move existing cluster a little towards the new one
        it->x = ofLerp(x, it->x, 0.05);
        it->y = ofLerp(y, it->y, 0.05);
        // existing cluster so increase its age to preserve it
        it->w++;
        introspector.addCircle(it->x, it->y, 3.0*1.0/Constants::WINDOW_WIDTH, ofColor::red, true, 25); // introspection: large red circle is existing cluster centre that continues to exist
      }
    }
  }
  TS_STOP("update-clusterCentres");
}

void ofApp::decayClusters() {
  TS_START("decay-clusters");
  for (auto& p: clusterCentres) {
    p.w *= clusterDecayRateParameter;
  }
  // delete decayed clusterCentres
  clusterCentres.erase(std::remove_if(clusterCentres.begin(),
                                      clusterCentres.end(),
                                      [](const glm::vec4& n) { return n.w <= 1.0; }),
                       clusterCentres.end());
  TS_STOP("decay-clusters");
}

void ofApp::updateSom(float x, float y, float z) {
  TS_START("update-som");
  double instance[3] = { static_cast<double>(x), static_cast<double>(y), static_cast<double>(z) };
  som.updateMap(instance);
  TS_STOP("update-som");
}

void ofApp::drawForegroundNoteMark(float x, float y, ofFloatColor color) {
  foregroundFbo.getSource().begin();
  ofEnableBlendMode(OF_BLENDMODE_DISABLED);
  ofSetColor(color);
  ofDrawCircle(x*foregroundFbo.getWidth(), y*foregroundFbo.getHeight(), 15.0);
  foregroundFbo.getSource().end();
}

void ofApp::drawFluidNoteMark(float x, float y, ofFloatColor color) {
  fluidSimulation.getFlowValuesFbo().getSource().begin();
  ofEnableBlendMode(OF_BLENDMODE_DISABLED);
  ofSetColor(color);
  ofDrawCircle(x*Constants::FLUID_WIDTH, y*Constants::FLUID_HEIGHT, 3.0);
  fluidSimulation.getFlowValuesFbo().getSource().end();
}

void ofApp::update() {
  introspector.update();
  
  audioDataProcessorPtr->update();
  
  TSGL_START("update-fluid-simulation");
  fluidSimulation.update();
  TSGL_STOP("update-fluid-simulation");
  
  fadeShader.render(crystalFbo, {1.0, 1.0, 1.0, fadeCrystalsParameter});
  //  logisticFnShader.render(crystalFbo, glm::vec4 { 0.0, 0.0, 0.0, 1.0 });
  fadeShader.render(divisionsFbo, {1.0, 1.0, 1.0, fadeDivisionsParameter});
  fadeShader.render(foregroundFbo, {1.0, 1.0, 1.0, fadeForegroundParameter});
  translateShader.render(foregroundFbo, {0.000, 0.0003});
  
  std::vector<ofxAudioData::ValiditySpec> sampleValiditySpecs {
    {ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, false, validLowerRmsParameter},
    {ofxAudioAnalysisClient::AnalysisScalar::pitch, false, validLowerPitchParameter},
    {ofxAudioAnalysisClient::AnalysisScalar::pitch, true, validUpperPitchParameter}
  };
  
  if (audioDataProcessorPtr->isDataValid(sampleValiditySpecs)) {
    
    // fetch scalars from current note
    float s = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::pitch, minPitchParameter, maxPitchParameter);// 700.0, 1300.0);
    float t = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, minRMSParameter, maxRMSParameter); //400.0, 4000.0, false);
    float u = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralKurtosis, minSpectralKurtosisParameter, maxSpectralKurtosisParameter);
    float v = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralCentroid, minSpectralCentroidParameter, maxSpectralCentroidParameter);
    
    // update recent notes and clusters
    updateRecentNotes(s, t, u, v);
    updateClusters();
    decayClusters();
    
    updateSom(s, t, v);
    ofFloatColor somColor = somColorAt(s, t);
    ofFloatColor darkSomColor = somColor; darkSomColor.setBrightness(0.3); darkSomColor.setSaturation(1.0);
    
    drawForegroundNoteMark(s, t, darkSomColor);
    drawFluidNoteMark(s, t, somColor);
    
    // draw arcs around longer-lasting clusterCentres into foreground
    {
      foregroundFbo.getSource().begin();
      ofEnableBlendMode(OF_BLENDMODE_ALPHA);
      ofNoFill();
      for (auto& p: clusterCentres) {
        if (p.w < 4.0) continue;
        ofFloatColor somColor = somColorAt(p.x, p.y);
        ofFloatColor darkSomColor = somColor; darkSomColor.setBrightness(0.6); darkSomColor.setSaturation(1.0); darkSomColor.a = 0.85;
        ofSetColor(darkSomColor);
        ofPolyline path;
        float radius = std::fmod(p.w*5.0, 550);
        path.arc(p.x*foregroundFbo.getWidth(), p.y*foregroundFbo.getHeight(), radius, radius, -180.0*(u+p.x), 180.0*(v+p.y), Constants::CIRCLE_RESOLUTION);
        path.draw();
      }
      foregroundFbo.getSource().end();
    }
    
    // draw circles around longer-lasting clusterCentres into fluid layer
    fluidSimulation.getFlowValuesFbo().getSource().begin();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    ofNoFill();
    ofSetColor(ofFloatColor(0.2, 0.2, 0.2, 0.6));
    for (auto& p: clusterCentres) {
      if (p.w < 5.0) continue;
      ofDrawCircle(p.x * Constants::FLUID_WIDTH, p.y * Constants::FLUID_HEIGHT, u * 100.0);
    }
    fluidSimulation.getFlowValuesFbo().getSource().end();
    
    {
      TS_START("update-fluid-clusters");
      for (auto& centre : clusterCentres) {
        float x = centre[0]; float y = centre[1];
        const float COL_FACTOR = 0.008;
        ofFloatColor color = somColorAt(x, y) * COL_FACTOR; color.a = 0.001;
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
      TS_STOP("update-fluid-clusters");
    }
    
    // Make fine structure based on frequent clusters
    TS_START("update-fine-structure");
    if (recentNoteXYs.size() > 50) { // arbitrary threshold: "enough" samples to start this process
      const std::vector<uint32_t>& clusteredNoteIds = std::get<1>(clusterResults);
      
      // find a cluster to work with
      auto clusterId = *(clusteredNoteIds.end() - 1); // could be begin() but maybe this gets the most recent note to start from
      
      // find some noteIds from that cluster
      std::vector<uint32_t> sampledClusterNoteIds;
      for(uint32_t i = clusteredNoteIds.size() - 1; i > clusteredNoteIds.size() - sampleNotesParameter; i--) {
        auto id = clusteredNoteIds[i];
        if (id == clusterId) sampledClusterNoteIds.push_back(i);
      }
      
      // draw crystals if we have at least a triangle
      if (sampledClusterNoteIds.size() > 2) {
        std::vector<glm::vec2> sampledClusterNoteXYs;
        glm::vec2 lastXY;
        for (uint32_t id : sampledClusterNoteIds) {
          float x = recentNoteXYs[id][0];
          float y = recentNoteXYs[id][1];
          // exclude consecutive positions with same X or Y
          glm::vec2 newXY = { x, y };
          if (lastXY.x != x && lastXY.y != y) sampledClusterNoteXYs.push_back(newXY);
          std::swap(lastXY, newXY);
        }
        
        // make normalised path from sampled notes
        ofPath path;
        for (const auto p : sampledClusterNoteXYs) {
          path.lineTo(p);
        }
        path.close();
        
        // find normalised path bounds
        ofRectangle pathBounds;
        for (const auto& polyline : path.getOutline()) {
          pathBounds = pathBounds.getUnion(polyline.getBoundingBox());
        }
        
        // ignore for bounds too small
        if (pathBounds.width > 1.0/100) {
          
          // add constrained divider lines extending the path segments,
          // and draw them into fluid layer
          fluidSimulation.getFlowValuesFbo().getSource().begin();
          {
            ofPushMatrix();
            ofScale(Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT);
            ofEnableBlendMode(OF_BLENDMODE_ALPHA);
            ofSetColor(ofFloatColor(0.0, 0.0, 0.0, 1.0));
            float width = 4.0 * 1.0 / Constants::FLUID_WIDTH;
            for (int i = 0; i != sampledClusterNoteXYs.size(); i++) {
              if (auto dividerLine = dividedArea.addConstrainedDividerLine(sampledClusterNoteXYs[i],
                                                                           sampledClusterNoteXYs[(i + 1) % sampledClusterNoteXYs.size()])) {
                dividerLine.value().draw(width);
              }
            };
            ofPopMatrix();
          }
          fluidSimulation.getFlowValuesFbo().getSource().end();
          
          // paint flat filled path into the fluid layer
          fluidSimulation.getFlowValuesFbo().getSource().begin();
          {
            ofPath fluidPath = path;
            fluidPath.scale(Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT);
            ofEnableBlendMode(OF_BLENDMODE_ALPHA);
            ofFloatColor fillColor = somColor; fillColor.a = 0.3;
            fluidPath.setColor(fillColor);
            fluidPath.setFilled(true);
            fluidPath.draw();
          }
          fluidSimulation.getFlowValuesFbo().getSource().end();
          
          // paint masked frozen fluid onto crystal layer
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
            
            // find a proportional scale to some limit to fill the mask with a reduced view of part of the frozen fluid
            constexpr float MAX_SCALE = 3.0;
            float scaleX = std::fminf(MAX_SCALE, 1.0 / pathBounds.width);
            float scaleY = std::fminf(MAX_SCALE, 1.0 / pathBounds.height);
            float scale = std::fminf(scaleX, scaleY);
            
            // draw scaled, coloured frozen fluid into the crystal layer through the mask
            crystalFbo.getSource().begin();
            {
              ofEnableBlendMode(OF_BLENDMODE_ADD);
              ofFloatColor fragmentColor = somColorAt(pathBounds.x, pathBounds.y)*0.3;
              ofSetColor(fragmentColor);
              maskShader.render(frozenFluid, crystalMaskFbo,
                                crystalFbo.getWidth(), crystalFbo.getHeight(),
                                false,
                                {pathBounds.x+pathBounds.width/2.0, pathBounds.y+pathBounds.height/2.0},
                                {scale, scale});
            }
            crystalFbo.getSource().end();
          }
        }
      }
    }
    TS_STOP("update-fine-structure");
    
    TS_START("update-divider");
    bool majorDividersChanged = dividedArea.updateUnconstrainedDividerLines(clusterCentres);
    if (majorDividersChanged) {
      TS_START("update-divider-draw-fluid");
      fluidSimulation.getFlowValuesFbo().getSource().begin();
      {
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        ofPushMatrix();
        ofScale(Constants::FLUID_WIDTH);
        const float lineWidth = 2.0 * 1.0 / Constants::FLUID_WIDTH;
        ofColor color = ofFloatColor(1.0, 1.0, 1.0, 0.25);
        ofSetColor(color);
        dividedArea.draw(0.0, lineWidth, 0.0);
        ofPopMatrix();
      }
      fluidSimulation.getFlowValuesFbo().getSource().end();
      TS_STOP("update-divider-draw-fluid");
      
      TS_START("update-divider-fetch-frozen");
      // fetching pixels from gpu is slow
      if (ofGetFrameNum() % 60 == 0.0) {
        ofPixels frozenPixels;
        fluidSimulation.getFlowValuesFbo().getSource().getTexture().readToPixels(frozenPixels);
        frozenFluid.allocate(frozenPixels);
      }
      TS_STOP("update-divider-fetch-frozen");
    }
    TS_STOP("update-divider");
    
  } //isDataValid()
  
  // draw divisions on divisionsFbo
  TS_START("update-draw-divisions");
  {
    divisionsFbo.getSource().begin();
    ofPushMatrix();
    ofScale(divisionsFbo.getWidth(), divisionsFbo.getHeight());
    const float maxLineWidth = 160.0 * 1.0 / divisionsFbo.getWidth();
    const float minLineWidth = 60.0 * 1.0 / divisionsFbo.getWidth();
    const ofFloatColor majorDividerColor { 0.0, 0.0, 0.0, 1.0 };
    const ofFloatColor minorDividerColor { 0.0, 0.0, 0.0, 1.0 };
    dividedArea.draw({}, { minLineWidth, maxLineWidth, majorDividerColor }, { minLineWidth/6.0f, minLineWidth/6.0f, minorDividerColor });
    ofPopMatrix();
    divisionsFbo.getSource().end();
  }
  TS_STOP("update-draw-divisions");
  
  if (dividedArea.constrainedDividerLines.size() > 2000) {
    dividedArea.deleteEarlyConstrainedDividerLines(50);
  }
}

ofFloatColor ofApp::somColorAt(float x, float y) const {
  double* somValue = som.getMapAt(x * Constants::SOM_WIDTH, y * Constants::SOM_HEIGHT);
  return ofFloatColor(somValue[0], somValue[1], somValue[2], 1.0);
}

//--------------------------------------------------------------
ofFbo ofApp::drawComposite() {
  compositeFbo.begin();
  ofClear(0, 255);
  
  // fluid
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 0.6));
    fluidSimulation.getFlowValuesFbo().getSource().draw(0.0, 0.0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
  }
  
  // foreground
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    foregroundFbo.draw(0, 0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
  }
  
  // crystals
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    crystalFbo.draw(0, 0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
  }
  
  // divisions
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    divisionsFbo.draw(0, 0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
  }
  
  compositeFbo.end();
  return compositeFbo;
}

void ofApp::draw() {
  drawComposite().draw(0.0, 0.0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
  
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
  if (key == 'S') {
    ofPixels pixels;
    drawComposite().readToPixels(pixels);
    ofSaveImage(pixels, ofFilePath::getUserHomeDir()+"/Documents/bells3/snapshot-"+ofGetTimestampString()+".png", OF_IMAGE_QUALITY_BEST);
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
