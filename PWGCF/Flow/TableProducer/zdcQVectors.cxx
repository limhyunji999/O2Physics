// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file   zdcQVectors.cxx
/// \author Noor Koster
/// \since  11/2024
/// \brief  In this task the energy calibration and recentring of Q-vectors constructed in the ZDCs will be done

#include <stdlib.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>
#include <typeinfo>
#include <memory>
#include <string>

#include "CCDB/BasicCCDBManager.h"
#include "Common/CCDB/EventSelectionParams.h"
#include "Common/CCDB/TriggerAliases.h"
#include "Common/Core/TrackSelection.h"
#include "Common/DataModel/TrackSelectionTables.h"
#include "Common/DataModel/Centrality.h"
#include "Common/DataModel/Multiplicity.h"
#include "Common/DataModel/EventSelection.h"

#include "Framework/AnalysisTask.h"
#include "Framework/AnalysisDataModel.h"
#include "Framework/HistogramRegistry.h"
#include "Framework/runDataProcessing.h"
#include "Framework/ASoAHelpers.h"
#include "Framework/RunningWorkflowInfo.h"
#include "Framework/StaticFor.h"

#include "DataFormatsParameters/GRPObject.h"
#include "DataFormatsParameters/GRPMagField.h"
#include "ReconstructionDataFormats/GlobalTrackID.h"
#include "ReconstructionDataFormats/Track.h"
#include "PWGCF/DataModel/SPTableZDC.h"

#include "TH1F.h"
#include "TH2F.h"
#include "TProfile.h"
#include "TObjArray.h"
#include "TF1.h"
#include "TFitResult.h"
#include "TCanvas.h"
#include "TSystem.h"
#include "TROOT.h"

#define O2_DEFINE_CONFIGURABLE(NAME, TYPE, DEFAULT, HELP) Configurable<TYPE> NAME{#NAME, DEFAULT, HELP};

using namespace o2;
using namespace o2::framework;
using namespace o2::framework::expressions;
using namespace o2::aod::track;
using namespace o2::aod::evsel;

// define my.....
using UsedCollisions = soa::Join<aod::Collisions, aod::EvSels, aod::CentFT0Cs>;
using BCsRun3 = soa::Join<aod::BCs, aod::Timestamps, aod::BcSels, aod::Run3MatchedToBCSparse>;

namespace o2::analysis::qvectortask
{

int counter = 0;

// step0 -> Energy calib
std::shared_ptr<TProfile2D> energyZN[10] = {{nullptr}};
std::shared_ptr<TH2> hQxvsQy[6] = {{nullptr}};

// <XX> <XY> <YX> and <YY>
std::shared_ptr<TProfile> hCOORDcorrelations[6][4] = {{nullptr}};

// Define histogrm names here to use same names for creating and later uploading and retrieving data from ccdb
// Energy calibration:
std::vector<TString> namesEcal(10, "");
std::vector<std::vector<TString>> names(5, std::vector<TString>()); //(1x 4d 4x 1d)
std::vector<TString> vnames = {"hvertex_vx", "hvertex_vy"};

// https://alice-notes.web.cern.ch/system/files/notes/analysis/620/017-May-31-analysis_note-ALICE_analysis_note_v2.pdf
std::vector<double> pxZDC = {-1.75, 1.75, -1.75, 1.75};
std::vector<double> pyZDC = {-1.75, -1.75, 1.75, 1.75};
double alphaZDC = 0.395;

// step 0 tm 5 A&C
std::vector<std::vector<std::vector<double>>> q(6, std::vector<std::vector<double>>(7, std::vector<double>(4, 0.0))); // 5 iterations with 5 steps, each with 4 values

// for energy calibration
std::vector<double> eZN(8);      // uncalibrated energy for the 2x4 towers (a1, a2, a3, a4, c1, c2, c3, c4)
std::vector<double> meanEZN(10); // mean energies from calibration histos (common A, t1-4 A,common C, t1-4C)
std::vector<double> e(8, 0.);    // calibrated energies (a1, a2, a3, a4, c1, c2, c3, c4))

//  Define variables needed to do the recentring steps.
double centrality = 0;
int runnumber = 0;
std::vector<double> v(3, 0); // vx, vy, vz
bool isSelected = true;

} // namespace o2::analysis::qvectortask

using namespace o2::analysis::qvectortask;

struct ZdcQVectors {

  Produces<aod::SPTableZDC> spTableZDC;

  ConfigurableAxis axisCent{"axisCent", {90, 0, 90}, "Centrality axis in 1% bins"};
  ConfigurableAxis axisCent10{"axisCent10", {9, 0, 90}, "Centrality axis in 10% bins"};
  ConfigurableAxis axisQ{"axisQ", {100, -2, 2}, "Q vector (xy) in ZDC"};
  ConfigurableAxis axisVxBig{"axisVxBig", {3, -0.01, 0.01}, "for Pos X of collision"};
  ConfigurableAxis axisVyBig{"axisVyBig", {3, -0.01, 0.01}, "for Pos Y of collision"};
  ConfigurableAxis axisVzBig{"axisVzBig", {3, -10, 10}, "for Pos Z of collision"};
  ConfigurableAxis axisVx{"axisVx", {10, -0.01, 0.01}, "for Pos X of collision"};
  ConfigurableAxis axisVy{"axisVy", {10, -0.01, 0.01}, "for Pos Y of collision"};
  ConfigurableAxis axisVz{"axisVz", {10, -10, 1}, "for vz of collision"};

  O2_DEFINE_CONFIGURABLE(cfgCutVertex, float, 10.0f, "Accepted z-vertex range")
  O2_DEFINE_CONFIGURABLE(cfgCutPtPOIMin, float, 0.2f, "Minimal.q pT for poi tracks")
  O2_DEFINE_CONFIGURABLE(cfgCutPtPOIMax, float, 10.0f, "Maximal pT for poi tracks")
  O2_DEFINE_CONFIGURABLE(cfgCutPtMin, float, 0.2f, "Minimal pT for ref tracks")
  O2_DEFINE_CONFIGURABLE(cfgCutPtMax, float, 3.0f, "Maximal pT for ref tracks")
  O2_DEFINE_CONFIGURABLE(cfgCutEta, float, 0.8f, "Eta range for tracks")
  O2_DEFINE_CONFIGURABLE(cfgCutChi2prTPCcls, float, 2.5, "Chi2 per TPC clusters")
  O2_DEFINE_CONFIGURABLE(cfgMagField, float, 99999, "Configurable magnetic field; default CCDB will be queried")
  O2_DEFINE_CONFIGURABLE(cfgEnergyCal, std::string, "Users/c/ckoster/ZDC/LHC23_zzh_pass4/Energy", "ccdb path for energy calibration histos")
  O2_DEFINE_CONFIGURABLE(cfgMeanv, std::string, "Users/c/ckoster/ZDC/LHC23_zzh_pass4/vmean", "ccdb path for mean v histos")
  O2_DEFINE_CONFIGURABLE(cfgMinEntriesSparseBin, int, 100, "Minimal number of entries allowed in 4D recentering histogram to use for recentering.")

  Configurable<std::vector<std::string>> cfgRec1{"cfgRec1", {"Users/c/ckoster/ZDC/LHC23_zzh_pass4/it1_step1", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it1_step2", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it1_step3", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it1_step4", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it1_step5"}, "ccdb paths for recentering calibration histos iteration 1"};
  Configurable<std::vector<std::string>> cfgRec2{"cfgRec2", {"Users/c/ckoster/ZDC/LHC23_zzh_pass4/it2_step1", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it2_step2", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it2_step3", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it2_step4", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it2_step5"}, "ccdb paths for recentering calibration histos iteration 2"};
  Configurable<std::vector<std::string>> cfgRec3{"cfgRec3", {"Users/c/ckoster/ZDC/LHC23_zzh_pass4/it3_step1", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it3_step2", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it3_step3", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it3_step4", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it3_step5"}, "ccdb paths for recentering calibration histos iteration 3"};
  Configurable<std::vector<std::string>> cfgRec4{"cfgRec4", {"Users/c/ckoster/ZDC/LHC23_zzh_pass4/it4_step1", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it4_step2", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it4_step3", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it4_step4", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it4_step5"}, "ccdb paths for recentering calibration histos iteration 4"};
  Configurable<std::vector<std::string>> cfgRec5{"cfgRec5", {"Users/c/ckoster/ZDC/LHC23_zzh_pass4/it5_step1", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it5_step2", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it5_step3", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it5_step4", "Users/c/ckoster/ZDC/LHC23_zzh_pass4/it5_step5"}, "ccdb paths for recentering calibration histos iteration 5"};

  //  Define output
  HistogramRegistry registry{"Registry"};

  Service<ccdb::BasicCCDBManager> ccdb;

  // keep track of calibration histos for each given step and iteration
  struct Calib {
    std::vector<std::vector<TList*>> calibList = std::vector<std::vector<TList*>>(7, std::vector<TList*>(8, nullptr));
    std::vector<std::vector<bool>> calibfilesLoaded = std::vector<std::vector<bool>>(7, std::vector<bool>(8, false));
    int atStep = 0;
    int atIteration = 0;
  } cal;

  void init(InitContext const&)
  {
    ccdb->setURL("http://alice-ccdb.cern.ch");
    ccdb->setCaching(true);
    ccdb->setLocalObjectValidityChecking();

    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    ccdb->setCreatedNotAfter(now);

    std::vector<const char*> sides = {"A", "C"};
    std::vector<const char*> capCOORDS = {"X", "Y"};

    // Tower mean energies vs. centrality used for tower gain equalisation
    for (int tower = 0; tower < 10; tower++) {
      namesEcal[tower] = TString::Format("hZN%s_mean_t%i_cent", sides[(tower < 5) ? 0 : 1], tower % 5);
      energyZN[tower] = registry.add<TProfile2D>(Form("Energy/%s", namesEcal[tower].Data()), Form("%s", namesEcal[tower].Data()), kTProfile2D, {{1, 0, 1}, axisCent});
    }

    // Qx_vs_Qy for each step for ZNA and ZNC
    for (int step = 0; step < 6; step++) {
      registry.add<TH2>(Form("step%i/QA/hSPplaneA", step), "hSPplaneA", kTH2D, {{100, -4, 4}, axisCent10});
      registry.add<TH2>(Form("step%i/QA/hSPplaneC", step), "hSPplaneC", kTH2D, {{100, -4, 4}, axisCent10});
      registry.add<TH2>(Form("step%i/QA/hSPplaneFull", step), "hSPplaneFull", kTH2D, {{100, -4, 4}, axisCent10});
      for (const auto& side : sides) {
        hQxvsQy[step] = registry.add<TH2>(Form("step%i/hZN%s_Qx_vs_Qy", step, side), Form("hZN%s_Qx_vs_Qy", side), kTH2F, {axisQ, axisQ});
      }
      int i = 0;
      for (const auto& COORD1 : capCOORDS) {
        for (const auto& COORD2 : capCOORDS) {
          // Now we get: <XX> <XY> & <YX> <YY> vs. Centrality
          hCOORDcorrelations[step][i] = registry.add<TProfile>(Form("step%i/QA/hQ%sA_Q%sC_vs_cent", step, COORD1, COORD2), Form("hQ%sA_Q%sC_vs_cent", COORD1, COORD2), kTProfile, {axisCent10});
          i++;
        }
      }

      // Add histograms for each step in the calibration process.
      // Sides is {A,C} and capcoords is {X,Y}
      for (const auto& side : sides) {
        for (const auto& coord : capCOORDS) {
          registry.add(Form("step%i/QA/hQ%s%s_vs_cent", step, coord, side), Form("hQ%s%s_vs_cent", coord, side), {HistType::kTProfile, {axisCent10}});
          registry.add(Form("step%i/QA/hQ%s%s_vs_vx", step, coord, side), Form("hQ%s%s_vs_vx", coord, side), {HistType::kTProfile, {axisVx}});
          registry.add(Form("step%i/QA/hQ%s%s_vs_vy", step, coord, side), Form("hQ%s%s_vs_vy", coord, side), {HistType::kTProfile, {axisVy}});
          registry.add(Form("step%i/QA/hQ%s%s_vs_vz", step, coord, side), Form("hQ%s%s_vs_vz", coord, side), {HistType::kTProfile, {axisVz}});

          if (step == 1 || step == 5) {
            TString name = TString::Format("hQ%s%s_mean_Cent_V_run", coord, side);
            registry.add(Form("step%i/%s", step, name.Data()), Form("hQ%s%s_mean_Cent_V_run", coord, side), {HistType::kTHnSparseD, {axisCent10, axisVxBig, axisVyBig, axisVzBig, axisQ}});
            if (step == 1)
              names[step - 1].push_back(name);
          }
          if (step == 2) {
            TString name = TString::Format("hQ%s%s_mean_cent_run", coord, side);
            registry.add<TProfile>(Form("step%i/%s", step, name.Data()), Form("hQ%s%s_mean_cent_run", coord, side), kTProfile, {axisCent});
            names[step - 1].push_back(name);
          }
          if (step == 3) {
            TString name = TString::Format("hQ%s%s_mean_vx_run", coord, side);
            registry.add<TProfile>(Form("step%i/%s", step, name.Data()), Form("hQ%s%s_mean_vx_run", coord, side), kTProfile, {axisVx});
            names[step - 1].push_back(name);
          }
          if (step == 4) {
            TString name = TString::Format("hQ%s%s_mean_vy_run", coord, side);
            registry.add<TProfile>(Form("step%i/%s", step, name.Data()), Form("hQ%s%s_mean_vy_run", coord, side), kTProfile, {axisVy});
            names[step - 1].push_back(name);
          }
          if (step == 5) {
            TString name = TString::Format("hQ%s%s_mean_vz_run", coord, side);
            registry.add<TProfile>(Form("step%i/%s", step, name.Data()), Form("hQ%s%s_mean_vz_run", coord, side), kTProfile, {axisVz});
            names[step - 1].push_back(name);
          }
        } // end of capCOORDS
      } // end of sides
    } // end of sum over steps

    // recentered q-vectors (to check what steps are finished in the end)
    registry.add("hStep", "hStep", {HistType::kTH1D, {{10, 0., 10.}}});
    registry.add("hIteration", "hIteration", {HistType::kTH1D, {{10, 0., 10.}}});

    registry.add<TProfile>("vmean/hvertex_vx", "hvertex_vx", kTProfile, {{1, 0., 1.}});
    registry.add<TProfile>("vmean/hvertex_vy", "hvertex_vy", kTProfile, {{1, 0., 1.}});
    registry.add<TProfile>("vmean/hvertex_vz", "hvertex_vz", kTProfile, {{1, 0., 1.}});

    registry.add<TH1>("QA/centrality_before", "centrality_before", kTH1D, {{200, 0, 100}});
    registry.add<TH1>("QA/centrality_after", "centrality_after", kTH1D, {{200, 0, 100}});

    registry.add<TProfile>("QA/ZNA_Energy", "ZNA_Energy", kTProfile, {{8, 0, 8}});
    registry.add<TProfile>("QA/ZNC_Energy", "ZNC_Energy", kTProfile, {{8, 0, 8}});
  }

  inline void fillRegistry(int iteration, int step)
  {
    if (step == 0 && iteration == 1) {
      registry.fill(HIST("hIteration"), iteration, 1);

      registry.fill(HIST("step1/hQXA_mean_Cent_V_run"), runnumber, centrality, v[0], v[1], v[2], q[0][step][0]);
      registry.fill(HIST("step1/hQYA_mean_Cent_V_run"), runnumber, centrality, v[0], v[1], v[2], q[0][step][1]);
      registry.fill(HIST("step1/hQXC_mean_Cent_V_run"), runnumber, centrality, v[0], v[1], v[2], q[0][step][2]);
      registry.fill(HIST("step1/hQYC_mean_Cent_V_run"), runnumber, centrality, v[0], v[1], v[2], q[0][step][3]);
      registry.fill(HIST("hStep"), step, 1);
    }

    if (step == 1) {
      registry.get<TProfile>(HIST("step2/hQXA_mean_cent_run"))->Fill(centrality, q[iteration][step][0]);
      registry.get<TProfile>(HIST("step2/hQYA_mean_cent_run"))->Fill(centrality, q[iteration][step][1]);
      registry.get<TProfile>(HIST("step2/hQXC_mean_cent_run"))->Fill(centrality, q[iteration][step][2]);
      registry.get<TProfile>(HIST("step2/hQYC_mean_cent_run"))->Fill(centrality, q[iteration][step][3]);
      registry.fill(HIST("hStep"), step, 1);
    }

    if (step == 2) {
      registry.get<TProfile>(HIST("step3/hQXA_mean_vx_run"))->Fill(v[0], q[iteration][step][0]);
      registry.get<TProfile>(HIST("step3/hQYA_mean_vx_run"))->Fill(v[0], q[iteration][step][1]);
      registry.get<TProfile>(HIST("step3/hQXC_mean_vx_run"))->Fill(v[0], q[iteration][step][2]);
      registry.get<TProfile>(HIST("step3/hQYC_mean_vx_run"))->Fill(v[0], q[iteration][step][3]);
      registry.fill(HIST("hStep"), step, 1);
    }

    if (step == 3) {
      registry.get<TProfile>(HIST("step4/hQXA_mean_vy_run"))->Fill(v[1], q[iteration][step][0]);
      registry.get<TProfile>(HIST("step4/hQYA_mean_vy_run"))->Fill(v[1], q[iteration][step][1]);
      registry.get<TProfile>(HIST("step4/hQXC_mean_vy_run"))->Fill(v[1], q[iteration][step][2]);
      registry.get<TProfile>(HIST("step4/hQYC_mean_vy_run"))->Fill(v[1], q[iteration][step][3]);
      registry.fill(HIST("hStep"), step, 1);
    }

    if (step == 4) {
      registry.get<TProfile>(HIST("step5/hQXA_mean_vz_run"))->Fill(v[2], q[iteration][step][0]);
      registry.get<TProfile>(HIST("step5/hQYA_mean_vz_run"))->Fill(v[2], q[iteration][step][1]);
      registry.get<TProfile>(HIST("step5/hQXC_mean_vz_run"))->Fill(v[2], q[iteration][step][2]);
      registry.get<TProfile>(HIST("step5/hQYC_mean_vz_run"))->Fill(v[2], q[iteration][step][3]);
      registry.fill(HIST("hStep"), step, 1);
    }

    if (step == 5) {
      registry.fill(HIST("step5/hQXA_mean_Cent_V_run"), centrality, v[0], v[1], v[2], q[iteration][step][0]);
      registry.fill(HIST("step5/hQYA_mean_Cent_V_run"), centrality, v[0], v[1], v[2], q[iteration][step][1]);
      registry.fill(HIST("step5/hQXC_mean_Cent_V_run"), centrality, v[0], v[1], v[2], q[iteration][step][2]);
      registry.fill(HIST("step5/hQYC_mean_Cent_V_run"), centrality, v[0], v[1], v[2], q[iteration][step][3]);
      registry.fill(HIST("hStep"), step, 1);
    }
  }

  inline void fillCommonRegistry(int iteration)
  {
    // loop for filling multiple histograms with different naming patterns
    //  Always fill the uncentered "raw" Q-vector histos!

    registry.fill(HIST("step0/hZNA_Qx_vs_Qy"), q[0][0][0], q[0][0][1]);
    registry.fill(HIST("step0/hZNC_Qx_vs_Qy"), q[0][0][2], q[0][0][3]);

    registry.fill(HIST("step0/QA/hQXA_QXC_vs_cent"), centrality, q[0][0][0] * q[0][0][2]);
    registry.fill(HIST("step0/QA/hQYA_QYC_vs_cent"), centrality, q[0][0][1] * q[0][0][3]);
    registry.fill(HIST("step0/QA/hQYA_QXC_vs_cent"), centrality, q[0][0][1] * q[0][0][2]);
    registry.fill(HIST("step0/QA/hQXA_QYC_vs_cent"), centrality, q[0][0][0] * q[0][0][3]);

    registry.fill(HIST("step0/QA/hQXA_vs_cent"), centrality, q[0][0][0]);
    registry.fill(HIST("step0/QA/hQYA_vs_cent"), centrality, q[0][0][1]);
    registry.fill(HIST("step0/QA/hQXC_vs_cent"), centrality, q[0][0][2]);
    registry.fill(HIST("step0/QA/hQYC_vs_cent"), centrality, q[0][0][3]);

    registry.fill(HIST("step0/QA/hQXA_vs_vx"), v[0], q[0][0][0]);
    registry.fill(HIST("step0/QA/hQYA_vs_vx"), v[0], q[0][0][1]);
    registry.fill(HIST("step0/QA/hQXC_vs_vx"), v[0], q[0][0][2]);
    registry.fill(HIST("step0/QA/hQYC_vs_vx"), v[0], q[0][0][3]);

    registry.fill(HIST("step0/QA/hQXA_vs_vy"), v[1], q[0][0][0]);
    registry.fill(HIST("step0/QA/hQYA_vs_vy"), v[1], q[0][0][1]);
    registry.fill(HIST("step0/QA/hQXC_vs_vy"), v[1], q[0][0][2]);
    registry.fill(HIST("step0/QA/hQYC_vs_vy"), v[1], q[0][0][3]);

    registry.fill(HIST("step0/QA/hQXA_vs_vz"), v[2], q[0][0][0]);
    registry.fill(HIST("step0/QA/hQYA_vs_vz"), v[2], q[0][0][1]);
    registry.fill(HIST("step0/QA/hQXC_vs_vz"), v[2], q[0][0][2]);
    registry.fill(HIST("step0/QA/hQYC_vs_vz"), v[2], q[0][0][3]);

    // add psi!!
    double psiA = 1.0 * std::atan2(q[0][0][2], q[0][0][0]);
    registry.fill(HIST("step0/QA/hSPplaneA"), psiA, centrality, 1);
    double psiC = 1.0 * std::atan2(q[0][0][3], q[0][0][1]);
    registry.fill(HIST("step0/QA/hSPplaneC"), psiC, centrality, 1);
    double psiFull = 1.0 * std::atan2(q[0][0][2] + q[0][0][3], q[0][0][0] + q[0][0][1]);
    registry.fill(HIST("step0/QA/hSPplaneFull"), psiFull, centrality, 1);

    static constexpr std::string_view SubDir[] = {"step1/", "step2/", "step3/", "step4/", "step5/"};
    static_for<0, 4>([&](auto Ind) {
      constexpr int Index = Ind.value;
      int indexRt = Index + 1;

      registry.fill(HIST(SubDir[Index]) + HIST("hZNA_Qx_vs_Qy"), q[iteration][indexRt][0], q[iteration][indexRt][1]);
      registry.fill(HIST(SubDir[Index]) + HIST("hZNC_Qx_vs_Qy"), q[iteration][indexRt][2], q[iteration][indexRt][3]);

      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXA_QXC_vs_cent"), centrality, q[iteration][indexRt][0] * q[iteration][indexRt][2]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYA_QYC_vs_cent"), centrality, q[iteration][indexRt][1] * q[iteration][indexRt][3]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYA_QXC_vs_cent"), centrality, q[iteration][indexRt][1] * q[iteration][indexRt][2]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXA_QYC_vs_cent"), centrality, q[iteration][indexRt][0] * q[iteration][indexRt][3]);

      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXA_vs_cent"), centrality, q[iteration][indexRt][0]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYA_vs_cent"), centrality, q[iteration][indexRt][1]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXC_vs_cent"), centrality, q[iteration][indexRt][2]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYC_vs_cent"), centrality, q[iteration][indexRt][3]);

      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXA_vs_vx"), v[0], q[iteration][indexRt][0]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYA_vs_vx"), v[0], q[iteration][indexRt][1]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXC_vs_vx"), v[0], q[iteration][indexRt][2]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYC_vs_vx"), v[0], q[iteration][indexRt][3]);

      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXA_vs_vy"), v[1], q[iteration][indexRt][0]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYA_vs_vy"), v[1], q[iteration][indexRt][1]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXC_vs_vy"), v[1], q[iteration][indexRt][2]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYC_vs_vy"), v[1], q[iteration][indexRt][3]);

      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXA_vs_vz"), v[2], q[iteration][indexRt][0]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYA_vs_vz"), v[2], q[iteration][indexRt][1]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQXC_vs_vz"), v[2], q[iteration][indexRt][2]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hQYC_vs_vz"), v[2], q[iteration][indexRt][3]);

      psiA = 1.0 * std::atan2(q[iteration][indexRt][2], q[iteration][indexRt][0]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hSPplaneA"), psiA, centrality, 1);
      psiC = 1.0 * std::atan2(q[iteration][indexRt][3], q[iteration][indexRt][1]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hSPplaneC"), psiC, centrality, 1);
      psiFull = 1.0 * std::atan2(q[iteration][indexRt][2] + q[iteration][indexRt][3], q[iteration][indexRt][0] + q[iteration][indexRt][1]);
      registry.fill(HIST(SubDir[Index]) + HIST("QA/hSPplaneFull"), psiFull, centrality, 1);
    });
  }

  void loadCalibrations(int iteration, int step, uint64_t timestamp, std::string ccdb_dir, std::vector<TString> names)
  {
    // iteration = 0 (Energy calibration) -> step 0 only
    // iteration 1,2,3,4,5 = recentering -> 5 steps per iteration (1x 4D + 4x 1D)
    if (ccdb_dir.empty() == false) {

      cal.calibList[iteration][step] = ccdb->getForTimeStamp<TList>(ccdb_dir, timestamp);

      if (cal.calibList[iteration][step]) {
        for (std::size_t i = 0; i < names.size(); i++) {
          TObject* obj = reinterpret_cast<TObject*>(cal.calibList[iteration][step]->FindObject(Form("%s", names[i].Data())));
          if (!obj) {
            if (counter < 1) {
              LOGF(error, "Object %s not found!!", names[i].Data());
              return;
            }
          }
          // Try to cast to TProfile
          if (TProfile* profile2D = dynamic_cast<TProfile*>(obj)) {
            if (profile2D->GetEntries() < 1) {
              if (counter < 1)
                LOGF(info, "%s (TProfile) is empty! Produce calibration file at given step", names[i].Data());
              cal.calibfilesLoaded[iteration][step] = false;
              return;
            }
            if (counter < 1)
              LOGF(info, "Loaded TProfile: %s", names[i].Data());
          } else if (TProfile2D* profile2D = dynamic_cast<TProfile2D*>(obj)) {
            if (profile2D->GetEntries() < 1) {
              if (counter < 1)
                LOGF(info, "%s (TProfile2D) is empty! Produce calibration file at given step", names[i].Data());
              cal.calibfilesLoaded[iteration][step] = false;
              return;
            }
            if (counter < 1)
              LOGF(info, "Loaded TProfile2D: %s", names[i].Data());
          } else if (THnSparse* sparse = dynamic_cast<THnSparse*>(obj)) {
            if (sparse->GetEntries() < 1) {
              if (counter < 1)
                LOGF(info, "%s (THnSparse) is empty! Produce calibration file at given step", names[i].Data());
              cal.calibfilesLoaded[iteration][step] = false;
              return;
            }
            if (counter < 1)
              LOGF(info, "Loaded THnSparse: %s", names[i].Data());
          }
        } // end of for loop
      } else {
        // when (cal.calib[iteration][step])=false!
        if (counter < 1)
          LOGF(warning, "Could not load TList with calibration histos from %s", ccdb_dir.c_str());
        cal.calibfilesLoaded[iteration][step] = false;
        return;
      }
      if (counter < 1)
        LOGF(info, "<--------OK----------> Calibrations loaded for cal.calibfilesLoaded[%i][%i]", iteration, step);
      cal.calibfilesLoaded[iteration][step] = true;
      cal.atIteration = iteration;
      cal.atStep = step;
      return;
    } else {
      if (counter < 1)
        LOGF(info, "<--------X-----------> Calibrations not loaded for iteration %i and step %i cfg = empty!", iteration, step);
    }
  }

  template <typename T>
  double getCorrection(int iteration, int step, const char* objName)
  {
    T* hist = nullptr;
    double calibConstant{0};

    hist = reinterpret_cast<T*>(cal.calibList[iteration][step]->FindObject(Form("%s", objName)));
    if (!hist) {
      LOGF(fatal, "%s not available.. Abort..", objName);
    }

    if (hist->InheritsFrom("TProfile2D")) {
      // needed for energy calibration!
      TProfile2D* h = reinterpret_cast<TProfile2D*>(hist);
      TString name = h->GetName();
      int binrunnumber = h->GetXaxis()->FindBin(TString::Format("%d", runnumber));
      int bin = h->GetYaxis()->FindBin(centrality);
      calibConstant = h->GetBinContent(binrunnumber, bin);
    } else if (hist->InheritsFrom("TProfile")) {
      TProfile* h = reinterpret_cast<TProfile*>(hist);
      TString name = h->GetName();
      int bin{};
      if (name.Contains("mean_vx"))
        bin = h->GetXaxis()->FindBin(v[0]);
      if (name.Contains("mean_vy"))
        bin = h->GetXaxis()->FindBin(v[1]);
      if (name.Contains("mean_vz"))
        bin = h->GetXaxis()->FindBin(v[2]);
      if (name.Contains("mean_cent"))
        bin = h->GetXaxis()->FindBin(centrality);
      if (name.Contains("vertex"))
        bin = h->GetXaxis()->FindBin(TString::Format("%i", runnumber));
      calibConstant = h->GetBinContent(bin);
    } else if (hist->InheritsFrom("THnSparse")) {
      std::vector<int> sparsePars;
      THnSparseD* h = reinterpret_cast<THnSparseD*>(hist);
      sparsePars.push_back(h->GetAxis(0)->FindBin(centrality));
      sparsePars.push_back(h->GetAxis(1)->FindBin(v[0]));
      sparsePars.push_back(h->GetAxis(2)->FindBin(v[1]));
      sparsePars.push_back(h->GetAxis(3)->FindBin(v[2]));

      for (std::size_t i = 0; i < sparsePars.size(); i++) {
        h->GetAxis(i)->SetRange(sparsePars[i], sparsePars[i]);
      }
      calibConstant = h->Projection(4)->GetMean();

      if (h->Projection(4)->GetEntries() < cfgMinEntriesSparseBin) {
        LOGF(debug, "1 entry in sparse bin! Not used... (increase binsize)");
        calibConstant = 0;
        isSelected = false;
      }
    }
    return calibConstant;
  }

  void fillAllRegistries(int iteration, int step)
  {
    for (int s = 0; s <= step; s++)
      fillRegistry(iteration, s);
    fillCommonRegistry(iteration);
  }

  void process(UsedCollisions::iterator const& collision,
               BCsRun3 const& /*bcs*/,
               aod::Zdcs const& /*zdcs*/)
  {
    // for Q-vector calculation
    //  A[0] & C[1]
    std::vector<double> sumZN(2, 0.);
    std::vector<double> xEnZN(2, 0.);
    std::vector<double> yEnZN(2, 0.);

    isSelected = true;

    auto cent = collision.centFT0C();

    if (cent < 0 || cent > 90) {
      isSelected = false;
      spTableZDC(runnumber, cent, v[0], v[1], v[2], 0, 0, 0, 0, isSelected, 0, 0);
      return;
    }

    registry.fill(HIST("QA/centrality_before"), cent);

    const auto& foundBC = collision.foundBC_as<BCsRun3>();

    if (!foundBC.has_zdc()) {
      isSelected = false;
      spTableZDC(runnumber, cent, v[0], v[1], v[2], 0, 0, 0, 0, isSelected, 0, 0);
      return;
    }

    v[0] = collision.posX();
    v[1] = collision.posY();
    v[2] = collision.posZ();
    centrality = cent;
    runnumber = foundBC.runNumber();

    const auto& zdcCol = foundBC.zdc();

    // Get the raw energies eZN[8] (not the common A,C)
    for (int tower = 0; tower < 8; tower++) {
      eZN[tower] = (tower < 4) ? zdcCol.energySectorZNA()[tower] : zdcCol.energySectorZNC()[tower % 4];
    }

    // load the calibration histos for iteration 0 step 0 (Energy Calibration)
    loadCalibrations(0, 0, foundBC.timestamp(), cfgEnergyCal.value, namesEcal);

    if (!cal.calibfilesLoaded[0][0]) {
      if (counter < 1) {
        LOGF(info, " --> No Energy calibration files found.. -> Only Energy calibration will be done. ");
      }
    }
    // load the calibrations for the mean v
    loadCalibrations(0, 1, foundBC.timestamp(), cfgMeanv.value, vnames);

    if (!cal.calibfilesLoaded[0][1]) {
      if (counter < 1)
        LOGF(warning, " --> No mean V found.. -> THis wil lead to wrong axis for vx, vy (will be created in vmean/)");
      registry.get<TProfile>(HIST("vmean/hvertex_vx"))->Fill(Form("%d", runnumber), v[0]);
      registry.get<TProfile>(HIST("vmean/hvertex_vy"))->Fill(Form("%d", runnumber), v[1]);
      registry.get<TProfile>(HIST("vmean/hvertex_vz"))->Fill(Form("%d", runnumber), v[2]);
    }

    if (counter < 1)
      LOGF(info, "=====================> .....Start Energy Calibration..... <=====================");

    bool isZNAhit = true;
    bool isZNChit = true;

    for (int i = 0; i < 8; ++i) {
      if (i < 4 && eZN[i] <= 0)
        isZNAhit = false;
      if (i > 3 && eZN[i] <= 0)
        isZNChit = false;
    }

    if (zdcCol.energyCommonZNA() <= 0)
      isZNAhit = false;
    if (zdcCol.energyCommonZNC() <= 0)
      isZNChit = false;

    // Fill to get mean energy per tower in 1% centrality bins
    for (int tower = 0; tower < 5; tower++) {
      if (tower == 0) {
        if (isZNAhit)
          energyZN[tower]->Fill(Form("%d", runnumber), cent, zdcCol.energyCommonZNA(), 1);
        if (isZNChit)
          energyZN[tower + 5]->Fill(Form("%d", runnumber), cent, zdcCol.energyCommonZNC(), 1);
        LOGF(debug, "Common A tower filled with: %i, %.2f, %.2f", runnumber, cent, zdcCol.energyCommonZNA());
      } else {
        if (isZNAhit)
          energyZN[tower]->Fill(Form("%d", runnumber), cent, eZN[tower - 1], 1);
        if (isZNChit)
          energyZN[tower + 5]->Fill(Form("%d", runnumber), cent, eZN[tower - 1 + 4], 1);
        LOGF(debug, "Tower ZNC[%i] filled with: %i, %.2f, %.2f", tower, runnumber, cent, eZN[tower - 1 + 4]);
      }
    }

    // if ZNA or ZNC not hit correctly.. do not use event in q-vector calculation
    if (!isZNAhit || !isZNChit) {
      counter++;
      isSelected = false;
      spTableZDC(runnumber, centrality, v[0], v[1], v[2], 0, 0, 0, 0, isSelected, 0, 0);
      return;
    }

    if (!cal.calibfilesLoaded[0][0]) {
      counter++;
      isSelected = false;
      spTableZDC(runnumber, centrality, v[0], v[1], v[2], 0, 0, 0, 0, isSelected, 0, 0);
      return;
    }

    if (counter < 1)
      LOGF(info, "files for step 0 (energy Calibraton) are open!");

    if (counter < 1) {
      LOGF(info, "=====================> .....Start Calculating Q-Vectors..... <=====================");
    }

    // Now start gain equalisation!
    // Fill the list with calibration constants.
    for (int tower = 0; tower < 10; tower++) {
      meanEZN[tower] = getCorrection<TProfile2D>(0, 0, namesEcal[tower].Data());
    }

    // Use the calibration constants but now only loop over towers 1-4
    int calibtower = 0;
    std::vector<int> towersNocom = {1, 2, 3, 4, 6, 7, 8, 9};

    for (const auto& tower : towersNocom) {
      if (meanEZN[tower] > 0) {
        double ecommon = (tower > 4) ? meanEZN[5] : meanEZN[0];
        e[calibtower] = eZN[calibtower] * (0.25 * ecommon) / meanEZN[tower];
      }
      calibtower++;
    }

    for (int i = 0; i < 4; i++) {
      float bincenter = i + .5;
      registry.fill(HIST("QA/ZNA_Energy"), bincenter, eZN[i]);
      registry.fill(HIST("QA/ZNA_Energy"), bincenter + 4, e[i]);
      registry.fill(HIST("QA/ZNC_Energy"), bincenter, eZN[i + 4]);
      registry.fill(HIST("QA/ZNC_Energy"), bincenter + 4, e[i + 4]);
    }

    // Now calculate Q-vector
    for (int tower = 0; tower < 8; tower++) {
      int side = (tower > 3) ? 1 : 0;
      int sector = tower % 4;
      double energy = std::pow(e[tower], alphaZDC);
      sumZN[side] += energy;
      xEnZN[side] += (side == 0) ? -1.0 * pxZDC[sector] * energy : pxZDC[sector] * energy;
      yEnZN[side] += pyZDC[sector] * energy;
    }

    // "QXA", "QYA", "QXC", "QYC"
    for (int i = 0; i < 2; ++i) {
      if (sumZN[i] > 0) {
        q[0][0][i * 2] = xEnZN[i] / sumZN[i];     // for QXA[0] and QXC[2]
        q[0][0][i * 2 + 1] = yEnZN[i] / sumZN[i]; // for QYA[1] and QYC[3]
      }
    }

    if (cal.calibfilesLoaded[0][1]) {
      if (counter < 1)
        LOGF(info, "=====================> Setting v to vmean!");
      v[0] = v[0] - getCorrection<TProfile>(0, 1, vnames[0].Data());
      v[1] = v[1] - getCorrection<TProfile>(0, 1, vnames[1].Data());
    }

    for (int iteration = 1; iteration < 6; iteration++) {
      std::vector<std::string> ccdbDirs;
      if (iteration == 1)
        ccdbDirs = cfgRec1.value;
      if (iteration == 2)
        ccdbDirs = cfgRec2.value;
      if (iteration == 3)
        ccdbDirs = cfgRec3.value;
      if (iteration == 4)
        ccdbDirs = cfgRec4.value;
      if (iteration == 5)
        ccdbDirs = cfgRec5.value;

      for (int step = 0; step < 5; step++) {
        loadCalibrations(iteration, step, foundBC.timestamp(), (ccdbDirs)[step], names[step]);
      }
    }

    if (counter < 1)
      LOGF(info, "We evaluate cal.atIteration=%i and cal.atStep=%i ", cal.atIteration, cal.atStep);

    if (cal.atIteration == 0) {
      if (counter < 1)
        LOGF(warning, "Calibation files missing!!! Output created with q-vectors right after energy gain eq. !!");
      if (isSelected)
        fillAllRegistries(0, 0);
      spTableZDC(runnumber, centrality, v[0], v[1], v[2], q[0][0][0], q[0][0][1], q[0][0][2], q[0][0][3], isSelected, 0, 0);
      counter++;
      return;
    } else {
      for (int iteration = 1; iteration <= cal.atIteration; iteration++) {
        for (int step = 0; step < cal.atStep + 1; step++) {
          if (cal.calibfilesLoaded[iteration][step]) {
            for (int i = 0; i < 4; i++) {
              if (step == 0) {
                if (iteration == 1) {
                  q[iteration][step + 1][i] = q[0][0][i] - getCorrection<THnSparse>(iteration, step, names[step][i].Data());
                } else {
                  q[iteration][step + 1][i] = q[iteration - 1][5][i] - getCorrection<THnSparse>(iteration, step, names[step][i].Data());
                }
              } else {
                q[iteration][step + 1][i] = q[iteration][step][i] - getCorrection<TProfile>(iteration, step, names[step][i].Data());
              }
            }
          } else {
            if (counter < 1)
              LOGF(warning, "Something went wrong in calibration loop! File not loaded but bool set to tue");
          } // end of (cal.calibLoaded)
        } // end of step
      } // end of iteration

      if (counter < 1)
        LOGF(info, "Output created with q-vectors at iteration %i and step %i!!!!", cal.atIteration, cal.atStep + 1);
      if (isSelected) {
        fillAllRegistries(cal.atIteration, cal.atStep + 1);
        registry.fill(HIST("QA/centrality_after"), centrality);
      }
      spTableZDC(runnumber, centrality, v[0], v[1], v[2], q[cal.atIteration][cal.atStep][0], q[cal.atIteration][cal.atStep][1], q[cal.atIteration][cal.atStep][2], q[cal.atIteration][cal.atStep][3], isSelected, cal.atIteration, cal.atStep);
      counter++;
      return;
    }
    LOGF(warning, "We return without saving table... -> THis is a problem");
  } // end of process
};

WorkflowSpec defineDataProcessing(ConfigContext const& cfgc)
{
  return WorkflowSpec{
    adaptAnalysisTask<ZdcQVectors>(cfgc)};
}
