// -*-Mode: C++;-*-

// * BeginRiceCopyright *****************************************************
//
// $HeadURL$
// $Id$
//
// --------------------------------------------------------------------------
// Part of HPCToolkit (hpctoolkit.org)
// 
// Information about sources of support for research and development of
// HPCToolkit is at 'hpctoolkit.org' and in 'README.Acknowledgments'.
// --------------------------------------------------------------------------
// 
// Copyright ((c)) 2002-2010, Rice University 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// 
// * Neither the name of Rice University (RICE) nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
// 
// This software is provided by RICE and contributors "as is" and any
// express or implied warranties, including, but not limited to, the
// implied warranties of merchantability and fitness for a particular
// purpose are disclaimed. In no event shall RICE or contributors be
// liable for any direct, indirect, incidental, special, exemplary, or
// consequential damages (including, but not limited to, procurement of
// substitute goods or services; loss of use, data, or profits; or
// business interruption) however caused and on any theory of liability,
// whether in contract, strict liability, or tort (including negligence
// or otherwise) arising in any way out of the use of this software, even
// if advised of the possibility of such damage. 
// 
// ******************************************************* EndRiceCopyright *

//***************************************************************************
//
// File:
//   $HeadURL$
//
// Purpose:
//   [The purpose of this file]
//
// Description:
//   [The set of functions, macros, etc. defined in the file]
//
//***************************************************************************

//************************* System Include Files ****************************

#include <iostream>
#include <fstream>

#include <string>
using std::string;

#include <vector>

//*************************** User Include Files ****************************

#include "Args.hpp"

#include <lib/analysis/CallPath.hpp>
#include <lib/analysis/Util.hpp>

#include <lib/support/diagnostics.h>
#include <lib/support/RealPathMgr.hpp>


//*************************** Forward Declarations ***************************

static int
realmain(int argc, char* const* argv);

static void
makeMetrics(const Analysis::Util::NormalizeProfileArgs_t& nArgs,
	    Prof::CallPath::Profile& prof);


//****************************************************************************

int 
main(int argc, char* const* argv) 
{
  int ret;

  try {
    ret = realmain(argc, argv);
  }
  catch (const Diagnostics::Exception& x) {
    DIAG_EMsg(x.message());
    exit(1);
  } 
  catch (const std::bad_alloc& x) {
    DIAG_EMsg("[std::bad_alloc] " << x.what());
    exit(1);
  }
  catch (const std::exception& x) {
    DIAG_EMsg("[std::exception] " << x.what());
    exit(1);
  } 
  catch (...) {
    DIAG_EMsg("Unknown exception encountered!");
    exit(2);
  }

  return ret;
}


static int
realmain(int argc, char* const* argv) 
{
  Args args;
  args.parse(argc, argv);

  RealPathMgr::singleton().searchPaths(args.searchPathStr());

  // ------------------------------------------------------------
  // Form one CCT from profile data
  // ------------------------------------------------------------
  Analysis::Util::NormalizeProfileArgs_t nArgs = 
    Analysis::Util::normalizeProfileArgs(args.profileFiles);

  if ( !(nArgs.paths->size() <= 32 || args.isHPCProfForce) ) {
    DIAG_Throw("There are " << nArgs.paths->size() << " profile files to process. As a sanity check, " << args.getCmd() << " limits the number of profile files it processes.  Use the --force option to remove this limit or use hpcprof-mpi.");
  }

  int mergeTy = Prof::CallPath::Profile::Merge_CreateMetric;
  Analysis::Util::UIntVec* groupMap =
    (nArgs.groupMax > 1) ? nArgs.groupMap : NULL;

  uint rFlags = 0; //Prof::CallPath::Profile::RFlg_MakeInclExcl;
  uint mrgFlags = (Prof::CCT::MrgFlg_NormalizeTraceFileY);

  Prof::CallPath::Profile* prof =
    Analysis::CallPath::read(*nArgs.paths, groupMap, mergeTy, rFlags, mrgFlags);

  // ------------------------------------------------------------
  // Overlay static structure with CCT's dynamic call paths
  // ------------------------------------------------------------
  Prof::Struct::Tree* structure = new Prof::Struct::Tree("");
  if (!args.structureFiles.empty()) {
    Analysis::CallPath::readStructure(structure, args);
  }
  prof->structure(structure);

  Analysis::CallPath::overlayStaticStructureMain(*prof, args.agent,
						 args.doNormalizeTy);
  
  prof->cct()->makeDensePreorderIds();

  // -------------------------------------------------------
  // Create summary metrics
  // -------------------------------------------------------

  // TODO: generate non-finalized metrics
  if (0) {
    makeMetrics(nArgs, *prof);
    
    Analysis::CallPath::applySummaryMetricAgents(*prof, args.agent);
  }
  
  nArgs.destroy();

  // ------------------------------------------------------------
  // Generate Experiment database
  // ------------------------------------------------------------

  if (args.title.empty()) {
    args.title = prof->name();
  }

  // Currently we do not generate thread-level metric db
  Prof::Metric::Mgr* mMgr = prof->metricMgr();
  for (uint i = 0; i < mMgr->size(); i++) {
    Prof::Metric::ADesc* m = mMgr->metric(i);
    if (m->hasDBInfo()) {
      m->zeroDBInfo();
    }
  }

  args.makeDatabaseDir();
  Analysis::CallPath::makeDatabase(*prof, args);

  delete prof;
  return 0;
}

//****************************************************************************


static void
makeMetrics(const Analysis::Util::NormalizeProfileArgs_t& nArgs,
	    Prof::CallPath::Profile& prof)
{
  Prof::Metric::Mgr& mMgr = *prof.metricMgr();

  Prof::CCT::ANode* cctRoot = prof.cct()->root();

  // -------------------------------------------------------
  // create derived metrics
  // -------------------------------------------------------
  uint numSrc = mMgr.size();
  uint mSrcBeg = 0, mSrcEnd = numSrc; // [ )

  uint numDrvd = 0;
  uint mDrvdBeg = 0, mDrvdEnd = 0; // [ )

  mDrvdBeg = mMgr.makeSummaryMetrics(mSrcBeg, mSrcEnd);
  if (mDrvdBeg != Prof::Metric::Mgr::npos) {
    mDrvdEnd = mMgr.size();
    numDrvd = (mDrvdEnd - mDrvdBeg);
  }

  for (uint mId = mSrcBeg; mId < mSrcEnd; ++mId) {
    Prof::Metric::ADesc* m = mMgr.metric(mId);
    m->isVisible(false);
  }


  // -------------------------------------------------------
  // aggregate sampled metrics (in batch)
  // -------------------------------------------------------

  VMAIntervalSet ivalsetIncl;
  VMAIntervalSet ivalsetExcl;

  for (uint mId = mSrcBeg; mId < mSrcEnd; ++mId) {
    Prof::Metric::ADesc* m = mMgr.metric(mId);
    if (m->type() == Prof::Metric::ADesc::TyIncl) {
      ivalsetIncl.insert(VMAInterval(mId, mId + 1)); // [ )
    }
    else if (m->type() == Prof::Metric::ADesc::TyExcl) {
      ivalsetExcl.insert(VMAInterval(mId, mId + 1)); // [ )
    }
    m->isComputed(true); // slightly proleptic
  }

  cctRoot->aggregateMetricsIncl(ivalsetIncl);
  cctRoot->aggregateMetricsExcl(ivalsetExcl);


  // -------------------------------------------------------
  // compute derived metrics
  // -------------------------------------------------------
  cctRoot->computeMetrics(mMgr, mDrvdBeg, mDrvdEnd);

  for (uint i = mDrvdBeg; i < mDrvdEnd; ++i) {
    Prof::Metric::ADesc* m = mMgr.metric(i);
    m->isComputed(true);
  }
}
