#include <fstream>

#include "TFile.h"
#include "TTree.h"
#include "TRandom.h" 
#include "TFormula.h"
#include "TMath.h"

#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/interface/Run.h"

#include "TrackingTools/PatternTools/interface/Trajectory.h"
#include "TrackingTools/TrackFitters/interface/TrajectoryStateCombiner.h"

#include "Alignment/CommonAlignment/interface/Alignable.h"  
#include "Alignment/CommonAlignment/interface/AlignableDetUnit.h"
#include "Alignment/CommonAlignment/interface/AlignmentParameters.h"
#include "Alignment/CommonAlignment/interface/SurveyResidual.h"
#include "Alignment/CommonAlignmentAlgorithm/interface/AlignmentParameterStore.h"
#include "Alignment/CommonAlignmentAlgorithm/interface/AlignmentParameterSelector.h"
#include "Alignment/HIPAlignmentAlgorithm/interface/HIPUserVariables.h"
#include "Alignment/HIPAlignmentAlgorithm/interface/HIPUserVariablesIORoot.h"
#include "Alignment/MuonAlignment/interface/AlignableMuon.h"
#include <DataFormats/GeometrySurface/interface/LocalError.h> 
#include "Alignment/TrackerAlignment/interface/AlignableTracker.h"
#include "Alignment/CommonAlignment/interface/AlignableExtras.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "Geometry/Records/interface/TrackerTopologyRcd.h" 
#include "Geometry/Records/interface/IdealGeometryRecord.h"
#include "Geometry/CommonTopologies/interface/SurfaceDeformation.h"
#include "Geometry/CommonTopologies/interface/SurfaceDeformationFactory.h"

#include "CondFormats/AlignmentRecord/interface/GlobalPositionRcd.h"
#include "CondFormats/AlignmentRecord/interface/TrackerAlignmentRcd.h"
#include "FWCore/Framework/interface/ValidityInterval.h"
#include "FWCore/Framework/interface/ESTransientHandle.h"
#include "FWCore/Framework/interface/EventSetup.h"


#include "Alignment/HIPAlignmentAlgorithm/interface/HIPAlignmentAlgorithm.h"

// Constructor ----------------------------------------------------------------
HIPAlignmentAlgorithm::HIPAlignmentAlgorithm(
  const edm::ParameterSet& cfg
  ) :
  AlignmentAlgorithmBase(cfg),
  surveyResiduals_(cfg.getUntrackedParameter<std::vector<std::string> >("surveyResiduals"))
{
  // parse parameters
  verbose = cfg.getParameter<bool>("verbosity");
  outpath = cfg.getParameter<std::string>("outpath");
  outfilecore = cfg.getParameter<std::string>("outfile"); outfile = outfilecore;
  outfile2 = cfg.getParameter<std::string>("outfile2");
  struefile = cfg.getParameter<std::string>("trueFile");
  smisalignedfile = cfg.getParameter<std::string>("misalignedFile");
  salignedfile = cfg.getParameter<std::string>("alignedFile");
  siterationfile = cfg.getParameter<std::string>("iterationFile");
  suvarfilecore = cfg.getParameter<std::string>("uvarFile"); suvarfile = suvarfilecore;
  sparameterfile = cfg.getParameter<std::string>("parameterFile");
  ssurveyfile = cfg.getParameter<std::string>("surveyFile");

  outfile        =outpath+outfile;//Eventwise tree
  outfile2       =outpath+outfile2;//Alignablewise tree
  struefile      =outpath+struefile;
  smisalignedfile=outpath+smisalignedfile;
  salignedfile   =outpath+salignedfile;
  siterationfile =outpath+siterationfile;
  suvarfile      =outpath+suvarfile;
  sparameterfile =outpath+sparameterfile;
  ssurveyfile    =outpath+ssurveyfile;

  // parameters for APE
  theApplyAPE = cfg.getParameter<bool>("applyAPE");
  theAPEParameterSet = cfg.getParameter<std::vector<edm::ParameterSet> >("apeParam");

  themultiIOV = cfg.getParameter<bool>("multiIOV");
  theIOVrangeSet = cfg.getParameter<std::vector<unsigned> >("IOVrange");

  theMaxAllowedHitPull = cfg.getParameter<double>("maxAllowedHitPull");
  theMinimumNumberOfHits = cfg.getParameter<int>("minimumNumberOfHits");
  theMaxRelParameterError = cfg.getParameter<double>("maxRelParameterError");

  theApplyCutsPerComponent = cfg.getParameter<bool>("applyCutsPerComponent");
  theCutsPerComponent = cfg.getParameter<std::vector<edm::ParameterSet> >("cutsPerComponent");

  // for collector mode (parallel processing)
  isCollector=cfg.getParameter<bool>("collectorActive");
  theCollectorNJobs=cfg.getParameter<int>("collectorNJobs");
  theCollectorPath=cfg.getParameter<std::string>("collectorPath");
  theFillTrackMonitoring=cfg.getUntrackedParameter<bool>("fillTrackMonitoring");

  if (isCollector) edm::LogInfo("Alignment") << "[HIPAlignmentAlgorithm] Collector mode";

  theEventPrescale = cfg.getParameter<int>("eventPrescale");
  theCurrentPrescale = theEventPrescale;

  trackPs = cfg.getParameter<bool>("UsePreSelection");
  trackWt = cfg.getParameter<bool>("UseReweighting");
  Scale = cfg.getParameter<double>("Weight");
  uniEta = cfg.getParameter<bool>("UniformEta");
  IsCollision = cfg.getParameter<bool>("isCollision");
  SetScanDet=cfg.getParameter<std::vector<double> >("setScanDet");
  col_cut = cfg.getParameter<double>("CLAngleCut");
  cos_cut = cfg.getParameter<double>("CSAngleCut");

  edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::HIPAlignmentAlgorithm" << "Constructed";
}

// Call at beginning of job ---------------------------------------------------
void HIPAlignmentAlgorithm::initialize(
  const edm::EventSetup& setup,
  AlignableTracker* tracker, AlignableMuon* muon, AlignableExtras* extras,
  AlignmentParameterStore* store
  ){
  edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::initialize" << "Initializing...";

  alignableObjectId_ = std::make_unique<AlignableObjectId>(
    AlignableObjectId::commonObjectIdProvider(tracker, muon)
    );

  for (const auto& level: surveyResiduals_) theLevels.push_back(alignableObjectId_->stringToId(level));

  edm::ESHandle<Alignments> globalPositionRcd;

  const edm::ValidityInterval & validity = setup.get<TrackerAlignmentRcd>().validityInterval();
  const edm::IOVSyncValue first1 = validity.first();
  unsigned int firstrun = first1.eventID().run();
  if (themultiIOV){
    if (theIOVrangeSet.size()!=1){
      bool findMatchIOV=false;
      for (unsigned int iovl = 0; iovl < theIOVrangeSet.size(); iovl++){
        if (firstrun == theIOVrangeSet.at(iovl)){
          std::string iovapp = std::to_string(firstrun);
          iovapp.append(".root");
          iovapp.insert(0, "_");
          salignedfile.replace(salignedfile.end()-5, salignedfile.end(), iovapp);
          siterationfile.replace(siterationfile.end()-5, siterationfile.end(), iovapp);
          //sparameterfile.replace(sparameterfile.end()-5, sparameterfile.end(),iovapp);
          if (isCollector){
            outfile2.replace(outfile2.end()-5, outfile2.end(), iovapp);
            ssurveyfile.replace(ssurveyfile.end()-5, ssurveyfile.end(), iovapp);
            suvarfile.replace(suvarfile.end()-5, suvarfile.end(), iovapp);
          }

          findMatchIOV=true;
          break;
        }
      }
      if (!findMatchIOV) edm::LogError("Alignment") << "@SUB=HIPAlignmentAlgorithm::initialize" << "Didn't find the matched IOV file!";
    }
    else{
      std::string iovapp = std::to_string(theIOVrangeSet.at(0));
      iovapp.append(".root");
      iovapp.insert(0, "_");
      salignedfile.replace(salignedfile.end()-5, salignedfile.end(), iovapp);
      siterationfile.replace(siterationfile.end()-5, siterationfile.end(), iovapp);
    }
  }

  // accessor Det->AlignableDet
  theAlignableDetAccessor = std::make_unique<AlignableNavigator>(extras, tracker, muon);
  if (extras!=0) edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::initialize" << "AlignableNavigator initialized with AlignableExtras";

  // set alignmentParameterStore
  theAlignmentParameterStore=store;

  // get alignables
  theAlignables = theAlignmentParameterStore->alignables();

  // Config flags that specify different detectors
  {
    AlignmentParameterSelector selector(tracker, muon, extras);

    // APE parameters, clear if necessary
    theAPEParameters.clear();
    if (theApplyAPE){
      for (std::vector<edm::ParameterSet>::const_iterator setiter = theAPEParameterSet.begin(); setiter != theAPEParameterSet.end(); ++setiter){
        std::vector<Alignable*> alignables;

        selector.clear();
        edm::ParameterSet selectorPSet = setiter->getParameter<edm::ParameterSet>("Selector");
        std::vector<std::string> alignParams = selectorPSet.getParameter<std::vector<std::string> >("alignParams");
        if (alignParams.size() == 1  &&  alignParams[0] == std::string("selected")) alignables = theAlignables;
        else{
          selector.addSelections(selectorPSet);
          alignables = selector.selectedAlignables();
        }

        std::vector<double> apeSPar = setiter->getParameter<std::vector<double> >("apeSPar");
        std::vector<double> apeRPar = setiter->getParameter<std::vector<double> >("apeRPar");
        std::string function = setiter->getParameter<std::string>("function");

        if (apeSPar.size() != 3  ||  apeRPar.size() != 3)
          throw cms::Exception("BadConfig")
          << "apeSPar and apeRPar must have 3 values each"
          << std::endl;

        for (std::vector<double>::const_iterator i = apeRPar.begin(); i != apeRPar.end(); ++i) apeSPar.push_back(*i);

        if (function == std::string("linear")) apeSPar.push_back(0); // c.f. note in calcAPE
        else if (function == std::string("exponential")) apeSPar.push_back(1); // c.f. note in calcAPE
        else if (function == std::string("step")) apeSPar.push_back(2); // c.f. note in calcAPE
        else throw cms::Exception("BadConfig") << "APE function must be \"linear\", \"exponential\", or \"step\"." << std::endl;

        theAPEParameters.push_back(std::pair<std::vector<Alignable*>, std::vector<double> >(alignables, apeSPar));
      }
    }

    // Relative error per component instead of overall relative error
    theCutsPerComponent.clear();
    if (theApplyCutsPerComponent){
      for (std::vector<edm::ParameterSet>::const_iterator setiter = theCutsPerComponent.begin(); setiter != theCutsPerComponent.end(); ++setiter){
        std::vector<Alignable*> alignables;

        selector.clear();
        edm::ParameterSet selectorPSet = setiter->getParameter<edm::ParameterSet>("Selector");
        std::vector<std::string> alignParams = selectorPSet.getParameter<std::vector<std::string> >("alignParams");
        if (alignParams.size() == 1  &&  alignParams[0] == std::string("selected")) alignables = theAlignables;
        else{
          selector.addSelections(selectorPSet);
          alignables = selector.selectedAlignables();
        }

        double maxRelParError = setiter->getParameter<double>("maxRelParError");
        double maxHitPull = setiter->getParameter<double>("maxHitPull");
        int minNHits = setiter->getParameter<double>("minNHits");
        for (auto& ali : alignables){
          HIPAlignableSpecificParameters alispecs;
          alispecs.id = ali->id();
          alispecs.objId = ali->alignableObjectId();

          alispecs.maxRelParError = maxRelParError;
          alispecs.maxHitPull = maxHitPull;
          alispecs.minNHits = minNHits;

          theAlignableSpecifics.push_back(alispecs);
        }
      }
    }

  }

}

// Call at new loop -------------------------------------------------------------
void HIPAlignmentAlgorithm::startNewLoop(void){
  edm::LogInfo("Alignment")
    << "@SUB=HIPAlignmentAlgorithm::startNewLoop" << "Begin";

  // iterate over all alignables and attach user variables
  for (std::vector<Alignable*>::const_iterator it=theAlignables.begin(); it!=theAlignables.end(); it++){
    AlignmentParameters* ap = (*it)->alignmentParameters();
    int npar=ap->numSelected();
    HIPUserVariables* userpar = new HIPUserVariables(npar);
    ap->setUserVariables(userpar);
  }

  // try to read in alignment parameters from a previous iteration
  AlignablePositions theAlignablePositionsFromFile = theIO.readAlignableAbsolutePositions(theAlignables, salignedfile.c_str(), -1, ioerr);
  int numAlignablesFromFile = theAlignablePositionsFromFile.size();
  if (numAlignablesFromFile==0) { // file not there: first iteration 
    // set iteration number to 1 when needed
    if (isCollector) theIteration=0;
    else theIteration=1;
    edm::LogWarning("Alignment") << "@SUB=HIPAlignmentAlgorithm::startNewLoop" << "File not found for iteration " << theIteration;
  }
  else{ // there have been previous iterations
    edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::startNewLoop" << "Alignables Read " << numAlignablesFromFile;

    // get iteration number from file     
    theIteration = readIterationFile(siterationfile);
    // Where is the target for this?
    theIO.readAlignableAbsolutePositions(theAlignables, salignedfile.c_str(), theIteration, ioerr);

    // increase iteration
    if (ioerr==0){
      theIteration++;
      edm::LogWarning("Alignment") << "@SUB=HIPAlignmentAlgorithm::startNewLoop" << "Iteration increased by one!";
    }

    // now apply psotions of file from prev iteration
    edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::startNewLoop" << "Apply positions from file ...";
    theAlignmentParameterStore->applyAlignableAbsolutePositions(theAlignables, theAlignablePositionsFromFile, ioerr);
  }

  edm::LogWarning("Alignment") << "@SUB=HIPAlignmentAlgorithm::startNewLoop" << "Current Iteration number: " << theIteration;


  // book root trees
  bookRoot();

  // set alignment position error 
  setAlignmentPositionError();

  // run collector job if we are in parallel mode
  if (isCollector) collector();

  edm::LogInfo("Alignment")
    << "@SUB=HIPAlignmentAlgorithm::startNewLoop"
    << "End";
}

// Call at end of job ---------------------------------------------------------
void HIPAlignmentAlgorithm::terminate(const edm::EventSetup& iSetup){
  edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm] Terminating";

  // calculating survey residuals
  if (theLevels.size()>0){
    edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm] Using survey constraint";

    unsigned int nAlignable = theAlignables.size();
    edm::ESHandle<TrackerTopology> tTopoHandle;
    iSetup.get<IdealGeometryRecord>().get(tTopoHandle);
    const TrackerTopology* const tTopo = tTopoHandle.product();
    for (unsigned int i = 0; i < nAlignable; ++i){
      const Alignable* ali = theAlignables[i];
      AlignmentParameters* ap = ali->alignmentParameters();
      HIPUserVariables* uservar = dynamic_cast<HIPUserVariables*>(ap->userVariables());
      int nhit = uservar->nhit;

      // get position
      std::pair<int, int> tl = theAlignmentParameterStore->typeAndLayer(ali, tTopo);
      int tmp_Type = tl.first;
      int tmp_Layer = tl.second;
      GlobalPoint pos = ali->surface().position();
      float tmpz = pos.z();
      if (nhit< 1500 || (tmp_Type==5 && tmp_Layer==4 && fabs(tmpz)>90)){ // FIXME: Needs revision for hardcoded consts
        for (unsigned int l = 0; l < theLevels.size(); ++l){
          SurveyResidual res(*ali, theLevels[l], true);

          if (res.valid()){
            AlgebraicSymMatrix invCov = res.inverseCovariance();

            // variable for tree
            AlgebraicVector sensResid = res.sensorResidual();
            m3_Id = ali->id();
            m3_ObjId = theLevels[l];
            m3_par[0] = sensResid[0]; m3_par[1] = sensResid[1]; m3_par[2] = sensResid[2];
            m3_par[3] = sensResid[3]; m3_par[4] = sensResid[4]; m3_par[5] = sensResid[5];

            uservar->jtvj += invCov;
            uservar->jtve += invCov * sensResid;

            theTree3->Fill();
          }
        }
      }
    }
  }

  // write user variables
  HIPUserVariablesIORoot HIPIO;
  // don't store userVariable in main, to save time
  if (!isCollector) HIPIO.writeHIPUserVariables(theAlignables, suvarfile.c_str(), theIteration, false, ioerr);

  // now calculate alignment corrections...
  int ialigned=0;
  // iterate over alignment parameters
  for (std::vector<Alignable*>::const_iterator it=theAlignables.begin(); it!=theAlignables.end(); it++){
    Alignable* ali=(*it);
    AlignmentParameters* par = ali->alignmentParameters();

    if (SetScanDet.at(0)!=0){
      edm::LogWarning("Alignment") << "********Starting Scan*********";
      edm::LogWarning("Alignment") <<"det ID="<<SetScanDet.at(0)<<", starting position="<<SetScanDet.at(1)<<", step="<<SetScanDet.at(2)<<", currentDet = "<<ali->id();
    }

    if ((SetScanDet.at(0)!=0)&&(SetScanDet.at(0)!=1)&&(ali->id()!=SetScanDet.at(0))) continue;

    bool test = calcParameters(ali, SetScanDet.at(0), SetScanDet.at(1), SetScanDet.at(2));
    if (test){
      if (dynamic_cast<AlignableDetUnit*>(ali)!=0){
        std::vector<std::pair<int, SurfaceDeformation*>> pairs;
        ali->surfaceDeformationIdPairs(pairs);
        edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::terminate" << "The alignable contains " << pairs.size() << " surface deformations";
      }
      else edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::terminate" << "The alignable cannot contain surface deformations";

      theAlignmentParameterStore->applyParameters(ali);
      // set these parameters 'valid'
      ali->alignmentParameters()->setValid(true);
      // increase counter
      ialigned++;
    }
    else par->setValid(false);
  }
  //end looping over alignables

  edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm::terminate] Aligned units: " << ialigned;

  // fill alignable wise root tree
  fillRoot(iSetup);

  edm::LogWarning("Alignment")
    << "[HIPAlignmentAlgorithm] Writing aligned parameters to file: " << theAlignables.size()
    << ", for Iteration " << theIteration;


  // write user variables	
  if (isCollector) HIPIO.writeHIPUserVariables(theAlignables, suvarfile.c_str(), theIteration, false, ioerr);

  // write new absolute positions to disk
  theIO.writeAlignableAbsolutePositions(theAlignables, salignedfile.c_str(), theIteration, false, ioerr);

  // write alignment parameters to disk
  //theIO.writeAlignmentParameters(theAlignables, 
  //				 sparameterfile.c_str(),theIteration,false,ioerr);

  // write iteration number to file
  writeIterationFile(siterationfile, theIteration);

  // write out trees and close root file

  // eventwise tree
  if (theFillTrackMonitoring){
    theFile->cd();
    theTree->Write();
    hitTree->Write();
    delete hitTree;
    delete theTree;
    theFile->Close();
  }

  if (theLevels.size()>0){
    theFile3->cd();
    theTree3->Write();
    delete theTree3;
    theFile3->Close();
  }

  // alignable-wise tree is only filled once
  if (theIteration==1){ // only for 0th or 1st iteration
    theFile2->cd();
    theTree2->Write();
    delete theTree2;
    theFile2->Close();
  }
}

bool HIPAlignmentAlgorithm::processHit1D(
  const AlignableDetOrUnitPtr& alidet,
  const Alignable* ali,
  const TrajectoryStateOnSurface & tsos,
  const TransientTrackingRecHit* hit,
  double hitwt
  ){
  static const unsigned int hitDim = 1;

  // get trajectory impact point
  LocalPoint alvec = tsos.localPosition();
  AlgebraicVector pos(hitDim);
  pos[0] = alvec.x();

  // get impact point covariance
  AlgebraicSymMatrix ipcovmat(hitDim);
  ipcovmat[0][0] = tsos.localError().positionError().xx();

  // get hit local position and covariance
  AlgebraicVector coor(hitDim);
  coor[0] = hit->localPosition().x();

  AlgebraicSymMatrix covmat(hitDim);
  covmat[0][0] = hit->localPositionError().xx();

  // add hit and impact point covariance matrices
  covmat = covmat + ipcovmat;

  // calculate the x pull of this hit
  double xpull = 0.;
  if (covmat[0][0] != 0.) xpull = (pos[0] - coor[0])/sqrt(fabs(covmat[0][0]));

  // get Alignment Parameters
  AlignmentParameters* params = ali->alignmentParameters();
  const HIPAlignableSpecificParameters* alispecifics = findAlignableSpecs(ali);
  // get derivatives
  AlgebraicMatrix derivs2D = params->selectedDerivatives(tsos, alidet);
  // calculate user parameters
  int npar = derivs2D.num_row();
  AlgebraicMatrix derivs(npar, hitDim, 0); // This is jT

  for (int ipar=0; ipar<npar; ipar++){
    for (unsigned int idim=0; idim<hitDim; idim++){
      derivs[ipar][idim] = derivs2D[ipar][idim];
    }
  }

  // invert covariance matrix
  int ierr;
  covmat.invert(ierr);
  if (ierr != 0){
    edm::LogError("Alignment") << "@SUB=HIPAlignmentAlgorithm::processHit1D" << "Cov. matrix inversion failed!";
    return false;
  }

  double maxHitPullThreshold = ((!theApplyCutsPerComponent || alispecifics==0) ? theMaxAllowedHitPull : alispecifics->maxHitPull);
  bool useThisHit = (maxHitPullThreshold < 0.);
  useThisHit |= (fabs(xpull) < maxHitPullThreshold);
  if (!useThisHit){
    edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::processHit2D" << "Hit pull (x) " << xpull << " fails the cut " << maxHitPullThreshold;
    return false;
  }

  AlgebraicMatrix covtmp(covmat);
  AlgebraicMatrix jtvjtmp(derivs * covtmp *derivs.T());
  AlgebraicSymMatrix thisjtvj(npar);
  AlgebraicVector thisjtve(npar);
  thisjtvj.assign(jtvjtmp);
  thisjtve=derivs * covmat * (pos-coor);

  AlgebraicVector hitresidual(hitDim);
  hitresidual[0] = (pos[0] - coor[0]);

  AlgebraicMatrix hitresidualT;
  hitresidualT = hitresidual.T();

  // access user variables (via AlignmentParameters)
  HIPUserVariables* uservar = dynamic_cast<HIPUserVariables*>(params->userVariables());

  uservar->jtvj += hitwt*thisjtvj;
  uservar->jtve += hitwt*thisjtve;
  uservar->nhit++;

  //for alignable chi squared
  float thischi2;
  thischi2 = hitwt*(hitresidualT *covmat *hitresidual)[0];

  if (verbose && (thischi2/ static_cast<float>(uservar->nhit))>10.)
    edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm::processHit1D] Added to Chi2 the number " << thischi2 << " having "
      << uservar->nhit << " ndof"
      << ", X-resid " << hitresidual[0]
      << ", Cov^-1 matr (covmat):"
      << " [0][0] = " << covmat[0][0];

  uservar->alichi2 += thischi2;
  uservar->alindof += hitDim;

  return true;
}

bool HIPAlignmentAlgorithm::processHit2D(
  const AlignableDetOrUnitPtr& alidet,
  const Alignable* ali,
  const TrajectoryStateOnSurface & tsos,
  const TransientTrackingRecHit* hit,
  double hitwt
  ){
  static const unsigned int hitDim = 2;

  // get trajectory impact point
  LocalPoint alvec = tsos.localPosition();
  AlgebraicVector pos(hitDim);
  pos[0] = alvec.x();
  pos[1] = alvec.y();

  // get impact point covariance
  AlgebraicSymMatrix ipcovmat(hitDim);
  ipcovmat[0][0] = tsos.localError().positionError().xx();
  ipcovmat[1][1] = tsos.localError().positionError().yy();
  ipcovmat[0][1] = tsos.localError().positionError().xy();

  // get hit local position and covariance
  AlgebraicVector coor(hitDim);
  coor[0] = hit->localPosition().x();
  coor[1] = hit->localPosition().y();

  AlgebraicSymMatrix covmat(hitDim);
  covmat[0][0] = hit->localPositionError().xx();
  covmat[1][1] = hit->localPositionError().yy();
  covmat[0][1] = hit->localPositionError().xy();

  // add hit and impact point covariance matrices
  covmat = covmat + ipcovmat;

  // calculate the x pull and y pull of this hit
  double xpull = 0.;
  double ypull = 0.;
  if (covmat[0][0] != 0.) xpull = (pos[0] - coor[0])/sqrt(fabs(covmat[0][0]));
  if (covmat[1][1] != 0.) ypull = (pos[1] - coor[1])/sqrt(fabs(covmat[1][1]));

  // get Alignment Parameters
  AlignmentParameters* params = ali->alignmentParameters();
  const HIPAlignableSpecificParameters* alispecifics = findAlignableSpecs(ali);
  // get derivatives
  AlgebraicMatrix derivs2D = params->selectedDerivatives(tsos, alidet);
  // calculate user parameters
  int npar = derivs2D.num_row();
  AlgebraicMatrix derivs(npar, hitDim, 0); // This is jT

  for (int ipar=0; ipar<npar; ipar++){
    for (unsigned int idim=0; idim<hitDim; idim++){
      derivs[ipar][idim] = derivs2D[ipar][idim];
    }
  }

  // invert covariance matrix
  int ierr;
  covmat.invert(ierr);
  if (ierr != 0){
    edm::LogError("Alignment") << "@SUB=HIPAlignmentAlgorithm::processHit2D" << "Cov. matrix inversion failed!";
    return false;
  }

  double maxHitPullThreshold = ((!theApplyCutsPerComponent || alispecifics==0) ? theMaxAllowedHitPull : alispecifics->maxHitPull);
  bool useThisHit = (maxHitPullThreshold < 0.);
  useThisHit |= (fabs(xpull) < maxHitPullThreshold  &&  fabs(ypull) < maxHitPullThreshold);
  if (!useThisHit){
    edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::processHit2D" << "Hit pull (x,y) " << xpull << " , " << ypull << " fails the cut " << maxHitPullThreshold;
    return false;
  }

  AlgebraicMatrix covtmp(covmat);
  AlgebraicMatrix jtvjtmp(derivs * covtmp *derivs.T());
  AlgebraicSymMatrix thisjtvj(npar);
  AlgebraicVector thisjtve(npar);
  thisjtvj.assign(jtvjtmp);
  thisjtve=derivs * covmat * (pos-coor);

  AlgebraicVector hitresidual(hitDim);
  hitresidual[0] = (pos[0] - coor[0]);
  hitresidual[1] = (pos[1] - coor[1]);

  AlgebraicMatrix hitresidualT;
  hitresidualT = hitresidual.T();
  // access user variables (via AlignmentParameters)
  HIPUserVariables* uservar = dynamic_cast<HIPUserVariables*>(params->userVariables());

  uservar->jtvj += hitwt*thisjtvj;
  uservar->jtve += hitwt*thisjtve;
  uservar->nhit++;

  //for alignable chi squared
  float thischi2;
  thischi2 = hitwt*(hitresidualT *covmat *hitresidual)[0];

  if (verbose && (thischi2/ static_cast<float>(uservar->nhit))>10.)
    edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm::processHit2D] Added to Chi2 the number " << thischi2 << " having "
      << uservar->nhit << " ndof"
      << ", X-resid " << hitresidual[0]
      << ", Y-resid " << hitresidual[1]
      << ", Cov^-1 matr (covmat):"
      << " [0][0] = " << covmat[0][0]
      << " [0][1] = " << covmat[0][1]
      << " [1][0] = " << covmat[1][0]
      << " [1][1] = " << covmat[1][1];

  uservar->alichi2 += thischi2;
  uservar->alindof += hitDim;

  return true;
}

// Run the algorithm on trajectories and tracks -------------------------------
void HIPAlignmentAlgorithm::run(const edm::EventSetup& setup, const EventInfo &eventInfo){
  if (isCollector) return;

  TrajectoryStateCombiner tsoscomb;

  // AM: not really needed
  // AM: m_Ntracks = 0 should be sufficient
  int itr=0;
  m_Ntracks=0;
  //CY : hit info  
  m_sinTheta =0;
  m_angle = 0;
  m_detId =0;
  m_hitwt=1;

  // AM: what is this needed for?
  //theFile->cd();

  // loop over tracks  
  const ConstTrajTrackPairCollection &tracks = eventInfo.trajTrackPairs();
  for (ConstTrajTrackPairCollection::const_iterator it=tracks.begin(); it!=tracks.end(); ++it){
    const Trajectory* traj = (*it).first;
    const reco::Track* track = (*it).second;

    float pt    = track->pt();
    float eta   = track->eta();
    float phi   = track->phi();
    float p     = track->p();
    float chi2n = track->normalizedChi2();
    int   nhit  = track->numberOfValidHits();
    float d0    = track->d0();
    float dz    = track->dz();

    int nhpxb   = track->hitPattern().numberOfValidPixelBarrelHits();
    int nhpxf   = track->hitPattern().numberOfValidPixelEndcapHits();
    int nhtib   = track->hitPattern().numberOfValidStripTIBHits();
    int nhtob   = track->hitPattern().numberOfValidStripTOBHits();
    int nhtid   = track->hitPattern().numberOfValidStripTIDHits();
    int nhtec   = track->hitPattern().numberOfValidStripTECHits();

    if (verbose) edm::LogInfo("Alignment")
      << "New track pt,eta,phi,chi2n,hits: "
      << pt << ","
      << eta << ","
      << phi << ","
      << chi2n << ","
      << nhit;

    double ihitwt = 1;
    double trkwt = 1;

    //eta distribution from 2015 RunD, need to change formula for other runs
    TFormula* my_formula = new TFormula("formula", "2.51469/(2.51469+4.11684*x-16.7847*pow(x,2)+46.1574*pow(x,3)-55.22*pow(x,4)+29.5591*pow(x,5)-5.39816*pow(x,6))");
    if (uniEta) trkwt = Scale*(my_formula->Eval(fabs(eta)));
    else trkwt=Scale;
    delete my_formula;

    if (trackPs){
      double r = gRandom->Rndm();
      if (trkwt < r) continue;
    }
    else if (trackWt) ihitwt=trkwt;


    //edm::LogWarning("Alignment") << "UsingReweighting="<<trackWt<<",trkwt="<<trkwt<<",hitWt="<<ihitwt;

    // fill track parameters in root tree
    if (itr<MAXREC) {
      m_Nhits[itr]=nhit;
      m_Pt[itr]=pt;
      m_P[itr]=p;
      m_Eta[itr]=eta;
      m_Phi[itr]=phi;
      m_Chi2n[itr]=chi2n;
      m_nhPXB[itr]=nhpxb;
      m_nhPXF[itr]=nhpxf;
      m_nhTIB[itr]=nhtib;
      m_nhTOB[itr]=nhtob;
      m_nhTID[itr]=nhtid;
      m_nhTEC[itr]=nhtec;
      m_d0[itr]=d0;
      m_dz[itr]=dz;
      m_wt[itr]=ihitwt;
      itr++;
      m_Ntracks=itr;
    }

    std::vector<const TransientTrackingRecHit*> hitvec;
    std::vector<TrajectoryStateOnSurface> tsosvec;

    // loop over measurements	
    std::vector<TrajectoryMeasurement> measurements = traj->measurements();
    for (std::vector<TrajectoryMeasurement>::iterator im=measurements.begin(); im!=measurements.end(); ++im){

      TrajectoryMeasurement meas = *im;

      // const TransientTrackingRecHit* ttrhit = &(*meas.recHit());
      // const TrackingRecHit *hit = ttrhit->hit();
      const TransientTrackingRecHit* hit = &(*meas.recHit());

      if (hit->isValid() && theAlignableDetAccessor->detAndSubdetInMap(hit->geographicalId())){
        // this is the updated state (including the current hit)
        // TrajectoryStateOnSurface tsos=meas.updatedState();
        // combine fwd and bwd predicted state to get state 
        // which excludes current hit

        //////////Hit prescaling part 	 
        if (eventInfo.clusterValueMap()){
          // check from the PrescalingMap if the hit was taken. 	 
          // If not skip to the next TM 	 
          // bool hitTaken=false; 	 
          AlignmentClusterFlag myflag;

          int subDet = hit->geographicalId().subdetId();
          //take the actual RecHit out of the Transient one
          const TrackingRecHit *rechit=hit->hit();
          if (subDet>2) { // AM: if possible use enum instead of hard-coded value	 
            const std::type_info &type = typeid(*rechit);

            if (type == typeid(SiStripRecHit1D)){

              const SiStripRecHit1D* stripHit1D = dynamic_cast<const SiStripRecHit1D*>(rechit);
              if (stripHit1D){
                SiStripRecHit1D::ClusterRef stripclust(stripHit1D->cluster());
                // myflag=PrescMap[stripclust]; 	 
                myflag = (*eventInfo.clusterValueMap())[stripclust];
              }
              else
                edm::LogError("HIPAlignmentAlgorithm")
                  << "ERROR in <HIPAlignmentAlgorithm::run>: Dynamic cast of Strip RecHit failed! "
                  << "TypeId of the RecHit: " << className(*hit) <<std::endl;
            }//end if type = SiStripRecHit1D 	 
            else if (type == typeid(SiStripRecHit2D)){

              const SiStripRecHit2D* stripHit2D = dynamic_cast<const SiStripRecHit2D*>(rechit);
              if (stripHit2D){
                SiStripRecHit2D::ClusterRef stripclust(stripHit2D->cluster());
                // myflag=PrescMap[stripclust]; 	 
                myflag = (*eventInfo.clusterValueMap())[stripclust];
              }
              else{
                edm::LogError("HIPAlignmentAlgorithm")
                  << "ERROR in <HIPAlignmentAlgorithm::run>: Dynamic cast of Strip RecHit failed! "
                  // << "TypeId of the TTRH: " << className(*ttrhit) << std::endl; 	 
                  << "TypeId of the TTRH: " << className(*hit) << std::endl;
              }
            } //end if type == SiStripRecHit2D 	 
          } //end if hit from strips 	 
          else{
            const SiPixelRecHit* pixelhit= dynamic_cast<const SiPixelRecHit*>(rechit);
            if (pixelhit){
              SiPixelClusterRefNew  pixelclust(pixelhit->cluster());
              // myflag=PrescMap[pixelclust]; 	 
              myflag = (*eventInfo.clusterValueMap())[pixelclust];
            }
            else
              edm::LogError("HIPAlignmentAlgorithm")
                << "ERROR in <HIPAlignmentAlgorithm::run>: Dynamic cast of Pixel RecHit failed! "
                // << "TypeId of the TTRH: " << className(*ttrhit) << std::endl; 	 
                << "TypeId of the TTRH: " << className(*hit) << std::endl;
          } //end 'else' it is a pixel hit 	 
          // bool hitTaken=myflag.isTaken(); 	 
          if (!myflag.isTaken()){
            continue;
          }
        }//end if Prescaled Hits 	 
        //////////////////////////////// 	 

        TrajectoryStateOnSurface tsos = tsoscomb.combine(meas.forwardPredictedState(),
          meas.backwardPredictedState());

        if (tsos.isValid()){
          // hitvec.push_back(ttrhit);
          hitvec.push_back(hit);
          tsosvec.push_back(tsos);
        }

      } //hit valid
    }

    // transform RecHit vector to AlignableDet vector
    std::vector <AlignableDetOrUnitPtr> alidetvec = theAlignableDetAccessor->alignablesFromHits(hitvec);

    // get concatenated alignment parameters for list of alignables
    CompositeAlignmentParameters aap = theAlignmentParameterStore->selectParameters(alidetvec);

    std::vector<TrajectoryStateOnSurface>::const_iterator itsos=tsosvec.begin();
    std::vector<const TransientTrackingRecHit*>::const_iterator ihit=hitvec.begin();

    // loop over vectors(hit,tsos)
    while (itsos != tsosvec.end()) {

      // get AlignableDet for this hit
      const GeomDet* det = (*ihit)->det();
      // int subDet= (*ihit)->geographicalId().subdetId();
      uint32_t nhitDim = (*ihit)->dimension();

      AlignableDetOrUnitPtr alidet = theAlignableDetAccessor->alignableFromGeomDet(det);

      // get relevant Alignable
      Alignable* ali = aap.alignableFromAlignableDet(alidet);

      if (ali!=0) {
        const TrajectoryStateOnSurface & tsos=*itsos;

        //  LocalVector v = tsos.localDirection();
        //  double proj_z = v.dot(LocalVector(0,0,1));

        //In fact, sin_theta=Abs(mom_z)
        double mom_x = tsos.localDirection().x();
        double mom_y = tsos.localDirection().y();
        double mom_z = tsos.localDirection().z();
        double sin_theta = TMath::Abs(mom_z) / sqrt(pow(mom_x, 2)+pow(mom_y, 2)+pow(mom_z, 2));
        double angle = TMath::ASin(sin_theta);


        //Make cut on hit impact angle, reduce collision hits perpendicular to modules
        if (IsCollision){ if (angle>col_cut)ihitwt=0; }
        else{ if (angle<cos_cut)ihitwt=0; }
        m_angle = angle;
        m_sinTheta = sin_theta;
        m_detId = ali->id();
        m_hitwt = ihitwt;

        if (theFillTrackMonitoring) hitTree->Fill();

        if (ihitwt!=0.){
          switch (nhitDim){
          case 1:
            processHit1D(alidet, ali, *itsos, *ihit, ihitwt);
            break;
          case 2:
            processHit2D(alidet, ali, *itsos, *ihit, ihitwt);
            break;
          default:
            edm::LogError("HIPAlignmentAlgorithm")
              << "ERROR in <HIPAlignmentAlgorithm::run>: Number of hit dimensions = "
              << nhitDim << " is not supported!"
              << std::endl;
            break;
          }
        }
      }

      itsos++;
      ihit++;
    }
  } // end of track loop

  // fill eventwise root tree (with prescale defined in pset)
  if (theFillTrackMonitoring) {
    theCurrentPrescale--;
    //edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm::run] theCurrentPrescale="<<theCurrentPrescale;
    if (theCurrentPrescale<=0) {
      theTree->Fill();
      theCurrentPrescale = theEventPrescale;
    }
  }
}

// ----------------------------------------------------------------------------
int HIPAlignmentAlgorithm::readIterationFile(std::string filename){
  int result;

  std::ifstream inIterFile(filename.c_str(), std::ios::in);
  if (!inIterFile) {
    edm::LogError("Alignment") << "[HIPAlignmentAlgorithm::readIterationFile] ERROR! "
      << "Unable to open Iteration file";
    result = -1;
  }
  else {
    inIterFile >> result;
    edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm::readIterationFile] "
      << "Read last iteration number from file: " << result;
  }
  inIterFile.close();

  return result;
}

// ----------------------------------------------------------------------------
void HIPAlignmentAlgorithm::writeIterationFile(std::string filename, int iter){
  std::ofstream outIterFile((filename.c_str()), std::ios::out);
  if (!outIterFile) edm::LogError("Alignment")
    << "[HIPAlignmentAlgorithm::writeIterationFile] ERROR: Unable to write Iteration file";
  else{
    outIterFile << iter;
    edm::LogWarning("Alignment") <<"[HIPAlignmentAlgorithm::writeIterationFile] writing iteration number to file: " << iter;
  }
  outIterFile.close();
}

// ----------------------------------------------------------------------------
// set alignment position error
void HIPAlignmentAlgorithm::setAlignmentPositionError(void){
  // Check if user wants to override APE
  if (!theApplyAPE){
    edm::LogInfo("Alignment") <<"[HIPAlignmentAlgorithm::setAlignmentPositionError] No APE applied";
    return; // NO APE APPLIED
  }

  edm::LogInfo("Alignment") <<"[HIPAlignmentAlgorithm::setAlignmentPositionError] Apply APE!";

  double apeSPar[3], apeRPar[3];
  for (std::vector<std::pair<std::vector<Alignable*>, std::vector<double> > >::const_iterator alipars = theAPEParameters.begin(); alipars != theAPEParameters.end(); ++alipars) {
    const std::vector<Alignable*> &alignables = alipars->first;
    const std::vector<double> &pars = alipars->second;

    apeSPar[0] = pars[0];
    apeSPar[1] = pars[1];
    apeSPar[2] = pars[2];
    apeRPar[0] = pars[3];
    apeRPar[1] = pars[4];
    apeRPar[2] = pars[5];

    int function = pars[6];

    // Printout for debug
    printf("APE: %u alignables\n", (unsigned int)alignables.size());
    for (int i=0; i<21; ++i) {
      double apelinstest=calcAPE(apeSPar, i, 0);
      double apeexpstest=calcAPE(apeSPar, i, 1);
      double apestepstest=calcAPE(apeSPar, i, 2);
      double apelinrtest=calcAPE(apeRPar, i, 0);
      double apeexprtest=calcAPE(apeRPar, i, 1);
      double apesteprtest=calcAPE(apeRPar, i, 2);
      printf("APE: iter slin sexp sstep rlin rexp rstep: %5d %12.5f %12.5f %12.5f %12.5f %12.5f %12.5f \n",
        i, apelinstest, apeexpstest, apestepstest, apelinrtest, apeexprtest, apesteprtest);
    }

    // set APE
    double apeshift = calcAPE(apeSPar, theIteration, function);
    double aperot = calcAPE(apeRPar, theIteration, function);
    theAlignmentParameterStore->setAlignmentPositionError(alignables, apeshift, aperot);
  }
}

// ----------------------------------------------------------------------------
// calculate APE
double HIPAlignmentAlgorithm::calcAPE(double* par, int iter, int function){
  double diter=(double)iter;
  if (function == 0) return std::max(par[1], par[0]+((par[1]-par[0])/par[2])*diter);
  else if (function == 1) return std::max(0., par[0]*(exp(-pow(diter, par[1])/par[2])));
  else if (function == 2){
    int ipar2 = (int)par[2];
    int step = iter/ipar2;
    double dstep = (double)step;
    return std::max(0., par[0] - par[1]*dstep);
  }
  else assert(false); // should have been caught in the constructor
}

// ----------------------------------------------------------------------------
// book root trees
void HIPAlignmentAlgorithm::bookRoot(void){
  TString tname="T1";
  char iterString[15];
  snprintf(iterString, sizeof(iterString), "%i", theIteration);
  tname.Append("_");
  tname.Append(iterString);

  // create ROOT files
  if (theFillTrackMonitoring){
    theFile = TFile::Open(outfile.c_str(), "update");
    theFile->cd();

    // book event-wise ROOT Tree

    TString tname_hit="T1_hit";
    tname_hit.Append("_");
    tname_hit.Append(iterString);

    theTree  = new TTree(tname, "Eventwise tree");

    //theTree->Branch("Run",     &m_Run,     "Run/I");
    //theTree->Branch("Event",   &m_Event,   "Event/I");
    theTree->Branch("Ntracks", &m_Ntracks, "Ntracks/I");
    theTree->Branch("Nhits", m_Nhits, "Nhits[Ntracks]/I");
    theTree->Branch("nhPXB", m_nhPXB, "nhPXB[Ntracks]/I");
    theTree->Branch("nhPXF", m_nhPXF, "nhPXF[Ntracks]/I");
    theTree->Branch("nhTIB", m_nhTIB, "nhTIB[Ntracks]/I");
    theTree->Branch("nhTOB", m_nhTOB, "nhTOB[Ntracks]/I");
    theTree->Branch("nhTID", m_nhTID, "nhTID[Ntracks]/I");
    theTree->Branch("nhTEC", m_nhTEC, "nhTEC[Ntracks]/I");
    theTree->Branch("Pt", m_Pt, "Pt[Ntracks]/F");
    theTree->Branch("P", m_P, "P[Ntracks]/F");
    theTree->Branch("Eta", m_Eta, "Eta[Ntracks]/F");
    theTree->Branch("Phi", m_Phi, "Phi[Ntracks]/F");
    theTree->Branch("Chi2n", m_Chi2n, "Chi2n[Ntracks]/F");
    theTree->Branch("d0", m_d0, "d0[Ntracks]/F");
    theTree->Branch("dz", m_dz, "dz[Ntracks]/F");
    theTree->Branch("wt", m_wt, "wt[Ntracks]/F");

    hitTree  = new TTree(tname_hit, "Hitwise tree");
    hitTree->Branch("Id", &m_detId, "Id/i");
    hitTree->Branch("sinTheta", &m_sinTheta, "sinTheta/F");
    hitTree->Branch("hitImpactAngle", &m_angle, "hitImpactAngle/F");
    hitTree->Branch("wt", &m_hitwt, "wt/F");
  }
  // book Alignable-wise ROOT Tree

  theFile2 = TFile::Open(outfile2.c_str(), "update");
  theFile2->cd();

  theTree2 = new TTree("T2", "Alignablewise tree");

  theTree2->Branch("Id", &m2_Id, "Id/i");
  theTree2->Branch("ObjId", &m2_ObjId, "ObjId/I");
  theTree2->Branch("Nhit", &m2_Nhit, "Nhit/I");
  theTree2->Branch("Type", &m2_Type, "Type/I");
  theTree2->Branch("Layer", &m2_Layer, "Layer/I");
  theTree2->Branch("Xpos", &m2_Xpos, "Xpos/F");
  theTree2->Branch("Ypos", &m2_Ypos, "Ypos/F");
  theTree2->Branch("Zpos", &m2_Zpos, "Zpos/F");
  theTree2->Branch("DeformationsType", &m2_dtype, "DeformationsType/S");
  theTree2->Branch("NumDeformations", &m2_nsurfdef, "NumDeformations/S");
  theTree2->Branch("Deformations", &m2_surfDef);

  // book survey-wise ROOT Tree only if survey is enabled
  if (theLevels.size()>0){
    edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm::bookRoot] Survey trees booked.";
    theFile3 = TFile::Open(ssurveyfile.c_str(), "update");
    theFile3->cd();
    theTree3 = new TTree(tname, "Survey Tree");
    theTree3->Branch("Id", &m3_Id, "Id/i");
    theTree3->Branch("ObjId", &m3_ObjId, "ObjId/I");
    theTree3->Branch("Par", &m3_par, "Par[6]/F");
  }
  edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm::bookRoot] Root trees booked.";
}

// ----------------------------------------------------------------------------
// fill alignable-wise root tree
void HIPAlignmentAlgorithm::fillRoot(const edm::EventSetup& iSetup){
  using std::setw;
  theFile2->cd();

  int naligned=0;

  //Retrieve tracker topology from geometry
  edm::ESHandle<TrackerTopology> tTopoHandle;
  //  iSetup.get<IdealGeometryRecord>().get(tTopoHandle);
  iSetup.get<TrackerTopologyRcd>().get(tTopoHandle);

  const TrackerTopology* const tTopo = tTopoHandle.product();

  for (std::vector<Alignable*>::const_iterator it=theAlignables.begin(); it!=theAlignables.end(); ++it){
    Alignable* ali = (*it);
    AlignmentParameters* dap = ali->alignmentParameters();

    // consider only those parameters classified as 'valid'
    if (dap->isValid()){
      // get number of hits from user variable
      HIPUserVariables* uservar = dynamic_cast<HIPUserVariables*>(dap->userVariables());
      m2_Nhit = uservar->nhit;

      // get type/layer
      std::pair<int, int> tl = theAlignmentParameterStore->typeAndLayer(ali, tTopo);
      m2_Type = tl.first;
      m2_Layer = tl.second;

      // get identifier (as for IO)
      m2_Id    = ali->id();
      m2_ObjId = ali->alignableObjectId();

      // get position
      GlobalPoint pos = ali->surface().position();
      m2_Xpos = pos.x();
      m2_Ypos = pos.y();
      m2_Zpos = pos.z();

      m2_surfDef.clear();
      {
        std::vector<std::pair<int, SurfaceDeformation*> > dali_id_pairs;
        SurfaceDeformation* dali_obj=0;
        SurfaceDeformationFactory::Type dtype = SurfaceDeformationFactory::kNoDeformations;
        std::vector<double> dali;
        if (1 == ali->surfaceDeformationIdPairs(dali_id_pairs)){
          dali_obj = dali_id_pairs[0].second;
          dali = dali_obj->parameters();
          dtype = (SurfaceDeformationFactory::Type)dali_obj->type();
        }
        for (auto& dit : dali) m2_surfDef.push_back((float)dit);
        m2_dtype = dtype;
        m2_nsurfdef = (short)m2_surfDef.size();
      }

      AlgebraicVector pars = dap->parameters();

      if (verbose) {
        edm::LogVerbatim("Alignment")
          << "------------------------------------------------------------------------\n"
          << " ALIGNABLE: " << setw(6) << naligned
          << '\n'
          << "hits: "   << setw(4) << m2_Nhit
          << " type: "  << setw(4) << m2_Type
          << " layer: " << setw(4) << m2_Layer
          << " id: "    << setw(4) << m2_Id
          << " objId: " << setw(4) << m2_ObjId
          << '\n'
          << std::fixed << std::setprecision(5)
          << "x,y,z: "
          << setw(12) << m2_Xpos
          << setw(12) << m2_Ypos
          << setw(12) << m2_Zpos
          << "\nDeformations type, nDeformations: "
          << setw(12) << m2_dtype
          << setw(12) << m2_nsurfdef
          << '\n'
          << "params: "
          << setw(12) << pars[0]
          << setw(12) << pars[1]
          << setw(12) << pars[2]
          << setw(12) << pars[3]
          << setw(12) << pars[4]
          << setw(12) << pars[5];
      }

      naligned++;
      theTree2->Fill();
    }
  }
}

// ----------------------------------------------------------------------------
bool HIPAlignmentAlgorithm::calcParameters(Alignable* ali, int setDet, double start, double step){
  edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::calcParameters" << "Begin: Processing detector " << ali->id();

  // Alignment parameters
  AlignmentParameters* par = ali->alignmentParameters();
  const HIPAlignableSpecificParameters* alispecifics = findAlignableSpecs(ali);
  // access user variables
  HIPUserVariables* uservar = dynamic_cast<HIPUserVariables*>(par->userVariables());
  int nhit = uservar->nhit;
  // The following variable is needed for the extended 1D/2D hit fix using
  // matrix shrinkage and expansion
  // int hitdim = uservar->hitdim;

  // Test nhits
  int minHitThreshold = ((!theApplyCutsPerComponent || alispecifics==0) ? theMinimumNumberOfHits : alispecifics->minNHits);
  if (setDet==0 && nhit<minHitThreshold){
    edm::LogWarning("Alignment") << "@SUB=HIPAlignmentAlgorithm::calcParameters" << "Skipping because number of hits = " << nhit << " <= " << minHitThreshold;
    par->setValid(false);
    return false;
  }

  AlgebraicSymMatrix jtvj = uservar->jtvj;
  AlgebraicVector jtve = uservar->jtve;

  // these are the alignment corrections+covariance (for selected params)
  int npar = jtve.num_row();
  AlgebraicVector params(npar);
  AlgebraicVector paramerr(npar);
  AlgebraicSymMatrix cov(npar*npar);

  // errors of parameters
  if (setDet==0){
    int ierr;
    AlgebraicSymMatrix jtvjinv = jtvj.inverse(ierr);
    if (ierr!=0){
      edm::LogError("Alignment") << "@SUB=HIPAlignmentAlgorithm::calcParameters" << "Matrix inversion failed!";
      return false;
    }
    params = -(jtvjinv * jtve);
    cov = jtvjinv;

    double maxRelErrThreshold = ((!theApplyCutsPerComponent || alispecifics==0) ? theMaxRelParameterError : alispecifics->maxRelParError);
    for (int i=0; i<npar; i++){
      double relerr=0;
      if (fabs(cov[i][i])>0.) paramerr[i] = sqrt(fabs(cov[i][i]));
      else paramerr[i] = params[i];
      if (params[i]!=0.) relerr = fabs(paramerr[i]/params[i]);
      if (maxRelErrThreshold>=0. && relerr>maxRelErrThreshold){
        edm::LogWarning("Alignment")
          << "@SUB=HIPAlignmentAlgorithm::calcParameters" << "RelError = " << relerr << " >= " << maxRelErrThreshold
          << ". Setting param = paramerr = 0 for component " << i;
        params[i]=0;
        paramerr[i]=0;
      }
    }
  }
  else{
    if (params.num_row()!=1){
      edm::LogError("Alignment") << "@SUB=HIPAlignmentAlgorithm::calcParameters" << "For scanning, please only turn on one parameter! check common_cff_py.txt";
      return false;
    }
    if (theIteration==1) params[0] = start;
    else params[0]=step;
    edm::LogWarning("Alignment") << "@SUB=HIPAlignmentAlgorithm::calcParameters" << "Parameters = " << params;
  }

  uservar->alipar=params;
  uservar->alierr=paramerr;

  AlignmentParameters* parnew = par->cloneFromSelected(params, cov);
  ali->setAlignmentParameters(parnew);
  parnew->setValid(true);

  edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::calcParameters" << "End: Processing detector " << ali->id();

  return true;
}

//-----------------------------------------------------------------------------
void HIPAlignmentAlgorithm::collector(void){
  edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::collector" <<  "Called for iteration " << theIteration;

  HIPUserVariablesIORoot HIPIO;
  for (int ijob=1; ijob<=theCollectorNJobs; ijob++) {
    edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::collector" << "Reading uservar for job " << ijob;

    std::stringstream ss;
    std::string str;
    ss << ijob;
    ss >> str;
    std::string uvfile = theCollectorPath+"/job"+str+"/"+suvarfilecore;

    std::vector<AlignmentUserVariables*> uvarvec = HIPIO.readHIPUserVariables(theAlignables, uvfile.c_str(), theIteration, ioerr);
    if (uvarvec.size()!=theAlignables.size())
      edm::LogWarning("Alignment") << "@SUB=HIPAlignmentAlgorithm::collector"
      << "Number of alignables = " << theAlignables.size() << " is not the same as number of user variables = " << uvarvec.size()
      << ". A mismatch might occur!";

    if (ioerr!=0){
      edm::LogError("Alignment") << "@SUB=HIPAlignmentAlgorithm::collector" << "Could not read user variable files for job " << ijob << " in iteration " << theIteration;
      continue;
    }

    // add
    std::vector<AlignmentUserVariables*> uvarvecadd;
    std::vector<AlignmentUserVariables*>::const_iterator iuvarnew=uvarvec.begin();
    for (std::vector<Alignable*>::const_iterator it=theAlignables.begin(); it!=theAlignables.end(); ++it){
      Alignable* ali = *it;
      AlignmentParameters* ap = ali->alignmentParameters();

      HIPUserVariables* uvarold = dynamic_cast<HIPUserVariables*>(ap->userVariables());
      HIPUserVariables* uvarnew = dynamic_cast<HIPUserVariables*>(*iuvarnew);

      HIPUserVariables* uvar = uvarold->clone();
      if (uvarnew!=0){
        uvar->nhit = (uvarold->nhit)+(uvarnew->nhit);
        uvar->jtvj = (uvarold->jtvj)+(uvarnew->jtvj);
        uvar->jtve = (uvarold->jtve)+(uvarnew->jtve);
        uvar->alichi2 = (uvarold->alichi2)+(uvarnew->alichi2);
        uvar->alindof = (uvarold->alindof)+(uvarnew->alindof);
        delete uvarnew; // Delete new user variables as they are added
      }

      uvarvecadd.push_back(uvar);
      iuvarnew++;
    }

    theAlignmentParameterStore->attachUserVariables(theAlignables, uvarvecadd, ioerr);

    // fill Eventwise Tree
    if (theFillTrackMonitoring){
      int nmontracks = fillEventwiseTree(uvfile.c_str(), theIteration, ioerr);
      uvfile = theCollectorPath+"/job"+str+"/"+outfilecore;
      edm::LogInfo("Alignment") << "@SUB=HIPAlignmentAlgorithm::collector"
        << "Added " << nmontracks << " tracks to the monitor tree";
    }
  } // end loop on jobs
}

//------------------------------------------------------------------------------------
int HIPAlignmentAlgorithm::fillEventwiseTree(const char* filename, int iter, int ierr){
  int totntrk = 0;
  char treeName[64];
  snprintf(treeName, sizeof(treeName), "T1_%d", iter);
  char hitTreeName[64];
  snprintf(hitTreeName, sizeof(hitTreeName), "T1_hit_%d", iter);

  //open the file "HIPAlignmentEvents.root" in the job directory
  TFile *jobfile = TFile::Open(filename, "READ");
  //grab the tree corresponding to this iteration
  TTree *jobtree = (TTree*)jobfile->Get(treeName);
  TTree *hittree = (TTree*)jobfile->Get(hitTreeName);
  //address and read the variables 
  static const int nmaxtrackperevent = 1000;
  int jobNtracks, jobNhitspertrack[nmaxtrackperevent], jobnhPXB[nmaxtrackperevent], jobnhPXF[nmaxtrackperevent], jobnhTIB[nmaxtrackperevent], jobnhTOB[nmaxtrackperevent], jobnhTID[nmaxtrackperevent], jobnhTEC[nmaxtrackperevent];
  float jobP[nmaxtrackperevent], jobPt[nmaxtrackperevent], jobEta[nmaxtrackperevent], jobPhi[nmaxtrackperevent];
  float jobd0[nmaxtrackperevent], jobwt[nmaxtrackperevent], jobdz[nmaxtrackperevent], jobChi2n[nmaxtrackperevent];
  float jobsinTheta, jobHitWt, jobangle;
  align::ID jobDetId;

  jobtree->SetBranchAddress("Ntracks", &jobNtracks);
  jobtree->SetBranchAddress("Nhits", jobNhitspertrack);
  jobtree->SetBranchAddress("nhPXB", jobnhPXB);
  jobtree->SetBranchAddress("nhPXF", jobnhPXF);
  jobtree->SetBranchAddress("nhTIB", jobnhTIB);
  jobtree->SetBranchAddress("nhTOB", jobnhTOB);
  jobtree->SetBranchAddress("nhTID", jobnhTID);
  jobtree->SetBranchAddress("nhTEC", jobnhTEC);
  jobtree->SetBranchAddress("Pt", jobPt);
  jobtree->SetBranchAddress("P", jobP);
  jobtree->SetBranchAddress("d0", jobd0);
  jobtree->SetBranchAddress("dz", jobdz);
  jobtree->SetBranchAddress("Eta", jobEta);
  jobtree->SetBranchAddress("Phi", jobPhi);
  jobtree->SetBranchAddress("Chi2n", jobChi2n);
  jobtree->SetBranchAddress("wt", jobwt);

  // CY: hit info
  hittree->SetBranchAddress("sinTheta", &jobsinTheta);
  hittree->SetBranchAddress("HitImpactAngle", &jobangle);
  hittree->SetBranchAddress("Id", &jobDetId);
  hittree->SetBranchAddress("wt", &jobHitWt);

  int ievent = 0;
  for (ievent=0; ievent<jobtree->GetEntries(); ++ievent){
    jobtree->GetEntry(ievent);

    //fill the collector tree with them

    //  TO BE IMPLEMENTED: a prescale factor like in run()
    m_Ntracks = jobNtracks;
    int ntrk = 0;
    while (ntrk<m_Ntracks){
      if (ntrk<MAXREC){
        totntrk = ntrk+1;
        m_Nhits[ntrk] = jobNhitspertrack[ntrk];
        m_Pt[ntrk] = jobPt[ntrk];
        m_P[ntrk] = jobP[ntrk];
        m_nhPXB[ntrk] = jobnhPXB[ntrk];
        m_nhPXF[ntrk] = jobnhPXF[ntrk];
        m_nhTIB[ntrk] = jobnhTIB[ntrk];
        m_nhTOB[ntrk] = jobnhTOB[ntrk];
        m_nhTID[ntrk] = jobnhTID[ntrk];
        m_nhTEC[ntrk] = jobnhTEC[ntrk];
        m_Eta[ntrk] = jobEta[ntrk];
        m_Phi[ntrk] = jobPhi[ntrk];
        m_Chi2n[ntrk] = jobChi2n[ntrk];
        m_d0[ntrk] = jobd0[ntrk];
        m_dz[ntrk] = jobdz[ntrk];
        m_wt[ntrk] = jobwt[ntrk];
      }//end if j<MAXREC
      else{
        edm::LogWarning("Alignment") << "[HIPAlignmentAlgorithm::fillEventwiseTree] Number of tracks in Eventwise tree exceeds MAXREC: "
          << m_Ntracks << "  Skipping exceeding tracks.";
        ntrk = m_Ntracks+1;
      }
      ++ntrk;
    }//end while loop
    theTree->Fill();
  }//end loop on i - entries in the job tree

  int ihit = 0;
  for (ihit=0; ihit<hittree->GetEntries(); ++ihit){
    hittree->GetEntry(ihit);
    m_angle = jobangle;
    m_sinTheta = jobsinTheta;
    m_detId = jobDetId;
    m_hitwt=jobHitWt;
    hitTree->Fill();
  }

  //clean up
  delete jobtree;
  delete hittree;
  jobfile->Close();

  return totntrk;
}

//-----------------------------------------------------------------------------------
HIPAlignableSpecificParameters* HIPAlignmentAlgorithm::findAlignableSpecs(const Alignable* ali){
  for (std::vector<HIPAlignableSpecificParameters>::iterator it=theAlignableSpecifics.begin(); it!=theAlignableSpecifics.end(); it++){
    if (ali->id()==it->id && ali->alignableObjectId()==it->objId) return &(*it);
  }
  return 0;
}
