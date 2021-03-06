// GARLI version 0.94 source code
// Copyright  2005 by Derrick J. Zwickl
// All rights reserved.
//
// This code may be used and modified for non-commercial purposes
// but redistribution in any form requires written permission.
// Please contact:
//
//  Derrick Zwickl
//	Integrative Biology, UT
//	1 University Station, C0930
//	Austin, TX  78712
//  email: zwickl@mail.utexas.edu
//
//	Note: In 2006  moving to NESCENT (The National
//	Evolutionary Synthesis Center) for a postdoc

#include <string.h>
#include <cassert>
#include <iostream>

using namespace std;

#include "configoptions.h"
#include "configreader.h"
#include "memchk.h"
#include "errorexception.h"

/////////////////////////////////////////////////////////////////////////
// GamlConfig::General methods //////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

GeneralGamlConfig::GeneralGamlConfig()	:bootstrapReps(0), outputMostlyUselessFiles(0), 
		outputPhylipTree(0), treeRejectionThreshold(100.0), dontInferProportionInvariant(0), 
		numPrecReductions(-1), precReductionFactor(-1.0), availableMemory(-1), 
		significantTopoChange(0.01), useflexrates(false), numratecats(4){
	//default values here //TODO
	logevery = 10;
	saveevery = 100;
	datafname = "datafname";
	ofprefix = "ofprefix";
	constraintfile = "\0";
	}

int GeneralGamlConfig::Read(const char* fname, bool isMaster /*=false*/)	{

	ConfigReader cr;	
	if (cr.Load(fname) != 0)	{
		printf("ERROR: GamlConfig::General::Read(%s) failed.\n", fname);
		return -1;
	}
	
	int errors = 0;
	errors += cr.SetSection("general");
	errors += cr.GetIntOption("logevery", logevery);
	errors += cr.GetIntOption("saveevery", saveevery);
	int found=cr.GetDoubleOption("megsclamemory", megsClaMemory, true);
	found += cr.GetDoubleOption("availablememory", availableMemory, true);
	if(found == -2) throw ErrorException("Error: either \"megsclamemory\" or \"availablememory\" must be specified in conf!");
	
	errors += cr.GetStringOption("datafname", datafname);
	errors += cr.GetStringOption("ofprefix", ofprefix);
	errors += cr.GetStringOption("streefname", streefname);
	cr.GetStringOption("constraintfile", constraintfile, true);
	errors += cr.GetIntOption("randseed", randseed);
	errors += cr.GetBoolOption("refinestart", refineStart);
	errors += cr.GetBoolOption("outputeachbettertopology", outputTreelog);
	errors += cr.GetBoolOption("enforcetermconditions", enforceTermConditions);
	errors += cr.GetIntOption("genthreshfortopoterm", lastTopoImproveThresh);
	errors += cr.GetDoubleOption("scorethreshforterm", improveOverStoredIntervalsThresh);
	cr.GetDoubleOption("significanttopochange", significantTopoChange, true);

	cr.GetBoolOption("outputmostlyuselessfiles", outputMostlyUselessFiles, true);
	cr.GetBoolOption("outputphyliptree", outputPhylipTree, true);
	cr.GetBoolOption("dontinferproportioninvariant", dontInferProportionInvariant, true);
	cr.GetBoolOption("useflexrates", useflexrates, true);
	cr.GetIntOption("numratecats", numratecats, true);	

	if(isMaster) errors += cr.SetSection("master");
	else errors += cr.SetSection("remote");
	
	errors += cr.GetIntOption("nindivs", min_nindivs);
	max_nindivs=min_nindivs;
	errors += cr.GetIntOption("holdover", holdover);
	errors += cr.GetDoubleOption("selectionintensity", selectionIntensity);
	errors += cr.GetDoubleOption("holdoverpenalty", holdoverPenalty);
	errors += cr.GetIntOption("stopgen", stopgen);
	errors += cr.GetIntOption("stoptime", stoptime);
	errors += cr.GetDoubleOption("startoptprec", startOptPrec);
	errors += cr.GetDoubleOption("minoptprec", minOptPrec);
	//changing this to specify either the number of reductions in the precision or the 
	//multiplier as before.  Prefer the number, since it should be easier to specify.  
	//
	found=0;
	found=cr.GetIntOption("numberofprecreductions", numPrecReductions, true);
	found += cr.GetDoubleOption("precreductionfactor", precReductionFactor, true);
	if(found == -2) throw ErrorException("Error: either \"numberofprecreductions\" (preferably) or \"precreductionfactor\" must be specified in conf!");
	
	errors += cr.GetDoubleOption("topoweight", topoWeight);
	errors += cr.GetDoubleOption("modweight", modWeight);	
	errors += cr.GetDoubleOption("brlenweight", brlenWeight);	
	errors += cr.GetDoubleOption("randnniweight", randNNIweight);
	errors += cr.GetDoubleOption("randsprweight", randSPRweight);	
	errors += cr.GetDoubleOption("limsprweight", limSPRweight);
	
	cr.GetDoubleOption("treerejectionthreshold", treeRejectionThreshold, true);

	cr.GetIntOption("bootstrapreps", bootstrapReps, true);
#ifdef MPI_VERSION
	if(bootstrapReps != 0) throw ErrorException("Sorry, Bootstrap not yet implemented in parallel GARLI!");
#endif

	cr.GetBoolOption("inferinternalstateprobs", inferInternalStateProbs, true);

#ifdef MPI_VERSION
	if(isMaster==false) errors += cr.GetDoubleOption("sendinterval", sendInterval);
#endif
	

#ifdef GANESH
	errors += cr.GetDoubleOption("randpecrweight", randPECRweight);
#endif
	errors += cr.GetIntOption("gammashapebrlen", gammaShapeBrlen);	
	errors += cr.GetIntOption("gammashapemodel", gammaShapeModel);

	errors += cr.GetIntOption("limsprrange", limSPRrange);
	errors += cr.GetIntOption("intervallength", intervalLength);
	errors += cr.GetIntOption("intervalstostore", intervalsToStore);
	errors += cr.GetDoubleOption("meanbrlenmuts", minBrlenMuts);
	maxBrlenMuts=minBrlenMuts;

#ifdef INCLUDE_PERTURBATION
	errors += cr.SetSection("perturbation");

	errors += cr.GetIntOption("perttype", pertType);
	errors += cr.GetDoubleOption("pertthresh", pertThresh);
	errors += cr.GetIntOption("minpertinterval", minPertInterval);
	errors += cr.GetIntOption("maxpertsnoimprove", maxPertsNoImprove);
	errors += cr.GetBoolOption("restartafterabandon", restartAfterAbandon);
	errors += cr.GetIntOption("gensbeforerestart", gensBeforeRestart);
	
	errors += cr.GetDoubleOption("ratchetproportion", ratchetProportion);
	errors += cr.GetDoubleOption("ratchetoffthresh", ratchetOffThresh);
	errors += cr.GetIntOption("ratchetmaxgen", ratchetMaxGen);
	
	errors += cr.GetIntOption("nnitargetaccepts", nniTargetAccepts);
	errors += cr.GetIntOption("nnimaxattempts", nniMaxAttempts);
	
	errors += cr.GetIntOption("numsprcycles", numSprCycles);
	errors += cr.GetIntOption("sprpertrange", sprPertRange);
#endif
	return errors;
}

int GeneralGamlConfig::Serialize(char** buf_, int* size_) const	{
	int& size = *size_;
	char*& buf = *buf_;
	
	// calculate the size first
	size = 0;
	size += sizeof(logevery);
	size += sizeof(saveevery);
	size += sizeof(megsClaMemory);
	
	size += (int)datafname.length() + 1;
	size += (int)method.length() + 1;
	size += (int)ofprefix.length() + 1;
	size += (int)streefname.length() + 1;
	
	// allocate the buffer
	buf = new char[size];
	
	// populate the buffer
	char* p = buf;
	
	for(int i=0;i<size;i++){
		p[i]=0;
		}
	
	memcpy(p, &logevery, sizeof(logevery));
	p += sizeof(logevery);
	
	memcpy(p, &saveevery, sizeof(saveevery));
	p += sizeof(saveevery);
	
	memcpy(p, &megsClaMemory, sizeof(megsClaMemory));
	p += sizeof(megsClaMemory);
	
	memcpy(p, datafname.c_str(), datafname.length()+1);
	p += datafname.length()+1;
	
	memcpy(p, method.c_str(), method.length()+1);
	p += method.length()+1;
	
	memcpy(p, ofprefix.c_str(), ofprefix.length()+1);
	p += ofprefix.length()+1;

	memcpy(p, streefname.c_str(), streefname.length()+1);
	p += streefname.length()+1;
	
	// sanity checks
	assert(p-buf == size);
	
	return size;
}

int GeneralGamlConfig::Deserialize(char* buf, int size)	{

	char* p = buf;
	
	memcpy(&logevery, p, sizeof(logevery));
	p += sizeof(logevery);
	
	memcpy(&saveevery, p, sizeof(saveevery));
	p += sizeof(saveevery);
	
	memcpy(&megsClaMemory, p, sizeof(megsClaMemory));
	p += sizeof(megsClaMemory);	

	datafname = p;
	p += strlen(p)+1;
	
	method = p;
	p += strlen(p)+1;
	
	ofprefix = p;
	p += strlen(p)+1;
	
	streefname = p;
	p += strlen(p)+1;
	
	// sanity checks
	assert(buf+size == p);
	
	return 0;
}

bool GeneralGamlConfig::operator==(const GeneralGamlConfig& rhs) const	{
	if (	logevery != rhs.logevery			||
			saveevery != rhs.saveevery			||
			datafname != rhs.datafname			||
			ofprefix != rhs.ofprefix	)
		return false;
	return true;
}

MasterGamlConfig::MasterGamlConfig() : GeneralGamlConfig() {
	
	
	}

int MasterGamlConfig::Read(const char* fname, bool isMaster){
	ConfigReader cr;	
	if (cr.Load(fname) != 0)	{
		printf("ERROR: GamlConfig::General::Read(%s) failed.\n", fname);
		return -1;
	}

	int errors = 0;

	errors += GeneralGamlConfig::Read(fname, true);

#ifdef MPI_VERSION
	
	errors += cr.SetSection("master");

	errors += cr.GetDoubleOption("startupdatethresh", startUpdateThresh);
	errors += cr.GetDoubleOption("minupdatethresh", minUpdateThresh);
//	errors += cr.GetDoubleOption("updatereductionfactor", updateReductionFactor);
				
//	errors += cr.GetIntOption("parallelinterval", subtreeInterval);
#ifdef SUBTREE_VERSION
	errors += cr.GetDoubleOption("subtreestartthresh", subtreeStartThresh);
	errors += cr.GetIntOption("minsubtreesize", minSubtreeSize);
	errors += cr.GetIntOption("targetsubtreesize", targetSubtreeSize);
	errors += cr.GetDoubleOption("orphanfactor", orphanFactor);
#else 
	subtreeStartThresh = 0.0;
	minSubtreeSize = -1;
	targetSubtreeSize = -1;
	orphanFactor = -1.0;
#endif

	errors += cr.GetIntOption("maxrecomindivs", maxRecomIndivs);
#endif
	return errors;
	}

/*
int GamlConfig::Master::Read(const char* fname)	{

	ConfigReader cr;	
	if (cr.Load(fname) != 0)	{
		printf("ERROR: GamlConfig::Master::Read(%s) failed.\n", fname);
		return -1;
	}
	
	int errors = 0;
	errors += cr.SetSection("master");
//	errors += cr.GetIntOption("crunchgens", crunchgens);
	errors += cr.GetIntOption("gammashape", gammashape);
	errors += cr.GetIntOption("holdover", holdover);
	errors += cr.GetIntRangeOption("nindivs", min_nindivs, max_nindivs);
//	errors += cr.GetIntOption("memory", memory);
//	errors += cr.GetIntOption("interval", interval);
//	errors += cr.GetIntOption("recvcount", recvcount);
	errors += cr.GetIntOption("stopgen", stopgen);
	errors += cr.GetDoubleRangeOption("meanbrlenmuts", min_brlen_muts, max_brlen_muts);
	
	errors += cr.GetDoubleOption("initialupdatethresh", initialUpdateThresh);
	errors += cr.GetDoubleOption("nonsubtreeupdatethresh", nonsubtreeUpdateThresh);
				
	errors += cr.GetIntOption("subtreeinterval", subtreeInterval);
	errors += cr.GetDoubleOption("subtreestartthresh", subtreeStartThresh);
	errors += cr.GetDoubleOption("subtreerecalcthresh", subtreeRecalcThresh);
	errors += cr.GetDoubleOption("subtreeupdatethresh", subtreeUpdateThresh);
	
	errors += cr.GetIntOption("perttype", pertType);
	errors += cr.GetDoubleOption("pertthresh", pertThresh);
	errors += cr.GetDoubleOption("pertamount", pertAmount);
	
	errors += cr.GetDoubleOption("selectionintensity", selectionIntensity);

	errors += cr.GetDoubleOption("startoptprec", startOptPrec);
	errors += cr.GetDoubleOption("minoptprec", minOptPrec);
	errors += cr.GetDoubleOption("topoweight", topoWeight);
	errors += cr.GetDoubleOption("modweight", modWeight);	
	errors += cr.GetDoubleOption("brlenweight", brlenWeight);	
	
	errors += cr.GetDoubleOption("randnniweight", randNNIweight);
	errors += cr.GetDoubleOption("randsprweight", randSPRweight);	
	errors += cr.GetDoubleOption("limsprweight", limSPRweight);
#ifdef GANESH
	errors += cr.GetDoubleOption("randpecrweight", randPECRweight);
#endif
	
	errors += cr.GetIntOption("limsprrange", limSPRrange);
	errors += cr.GetIntOption("intervallength", intervalLength);
	errors += cr.GetIntOption("intervalstostore", intervalsToStore);

	
		
	return -errors;
}

int GamlConfig::Master::Serialize(char** buf_, int* size_) const	{
	int& size = *size_;
	char*& buf = *buf_;
	
	// calculate the size first
	size = sizeof(*this);
	
	// allocate the buffer
	buf = new char[size];
	
	// populate the buffer
	char* p = buf;
	
	memcpy(p, this, sizeof(*this));
	p += sizeof(*this);
	
	// sanity checks
	assert(p-buf == size);
	
	return size;
}

int GamlConfig::Master::Deserialize(char* buf, int size)	{

	char* p = buf;
	
	memcpy(this, p, sizeof(*this));
	p += sizeof(*this);
	
	// sanity checks
	assert(buf+size == p);
	
	return 0;
}

bool GamlConfig::Master::operator==(const GamlConfig::Master& rhs) const	{
	if (memcmp(this, &rhs, sizeof(*this)) == 0)
		return true;
	return false;
}

/////////////////////////////////////////////////////////////////////////
// GamlConfig::Remote methods ///////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

GamlConfig::Remote::Remote()	{
	memset(this, 0, sizeof(*this));
}

int GamlConfig::Remote::Read(const char* fname)	{

	ConfigReader cr;	
	if (cr.Load(fname) != 0)	{
		printf("ERROR: GamlConfig::Remote::Read(%s) failed.\n", fname);
		return -1;
	}
	
	int errors = 0;
	errors += cr.SetSection("remote");
	errors += cr.GetIntOption("gammashape", gammashape);
	errors += cr.GetIntOption("holdover", holdover);
	errors += cr.GetIntRangeOption("nindivs", min_nindivs, max_nindivs);
	errors += cr.GetIntOption("interval", interval);
	errors += cr.GetIntOption("stopgen", stopgen);
	errors += cr.GetDoubleRangeOption("meanbrlenmuts", min_brlen_muts, max_brlen_muts);
	errors += cr.GetDoubleOption("selectionintensity", selectionIntensity);
	errors += cr.GetDoubleOption("startoptprec", startOptPrec);
	errors += cr.GetDoubleOption("minoptprec", minOptPrec);
	errors += cr.GetDoubleOption("topoweight", topoWeight);
	errors += cr.GetDoubleOption("modweight", modWeight);	
	errors += cr.GetDoubleOption("brlenweight", brlenWeight);	
	
	errors += cr.GetDoubleOption("randnniweight", randNNIweight);
	errors += cr.GetDoubleOption("randsprweight", randSPRweight);	
	errors += cr.GetDoubleOption("limsprweight", limSPRweight);
#ifdef GANESH
	errors += cr.GetDoubleOption("randpecrweight", randPECRweight);
#endif
	
	errors += cr.GetIntOption("limsprrange", limSPRrange);
	errors += cr.GetIntOption("intervallength", intervalLength);
	errors += cr.GetIntOption("intervalstostore", intervalsToStore);

	return -errors;
}

int GamlConfig::Remote::Serialize(char** buf_, int* size_) const	{
	int& size = *size_;
	char*& buf = *buf_;
	
	// calculate the size first
	size = sizeof(*this);
	
	// allocate the buffer
	buf = new char[size];
	
	// populate the buffer
	char* p = buf;
	
	memcpy(p, this, sizeof(*this));
	p += sizeof(*this);
	
	// sanity checks
	assert(p-buf == size);
	
	return size;
}

int GamlConfig::Remote::Deserialize(char* buf, int size)	{

	char* p = buf;
	
	memcpy(this, p, sizeof(*this));
	p += sizeof(*this);
	
	// sanity checks
	assert(buf+size == p);
	
	return 0;
}

bool GamlConfig::Remote::operator==(const GamlConfig::Remote& rhs) const	{
	if (memcmp(this, &rhs, sizeof(*this)) == 0)
		return true;
	return false;
}

/////////////////////////////////////////////////////////////////////////
// GamlConfig methods ///////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

GamlConfig::GamlConfig()	{
}

int GamlConfig::Read(const char* fname)	{
	int gerrors, merrors, rerrors;
	
	gerrors = -gc.Read(fname);
	if (gerrors > 0)	{
		printf("ERROR: GamlConfig::Read(): reading [general] produced %d errors.\n", gerrors);
	}
	
	merrors = -mc.Read(fname);
	if (merrors > 0)	{
		printf("ERROR: GamlConfig::Read(): reading [master] produced %d errors.\n", merrors);
	}
	
	rerrors = -rc.Read(fname);
	if (rerrors > 0)	{
		printf("ERROR: GamlConfig::Read(): reading [remote] produced %d errors.\n", rerrors);
	}
	
	if (gerrors || merrors || rerrors)
		return -1;
		
	return 0;
}

int GamlConfig::Serialize(char** buf_, int* size_) const{
	//there's no need to serialize and send the master conf info	
	
	int& size = *size_;
	char*& buf = *buf_;
	
	int gsize, msize, rsize;
	char *gbuf, *mbuf, *rbuf;
	
	gc.Serialize(&gbuf, &gsize);
//	mc.Serialize(&mbuf, &msize);
	rc.Serialize(&rbuf, &rsize);
	
	size = gsize + rsize + sizeof(int)*2;
//	size = gsize + msize + rsize + sizeof(int)*3;
	
	char* p = buf = new char[size];
	
	// put in the sizes
	
	memcpy(p, &gsize, sizeof(gsize));
	p += sizeof(gsize);
	
//	memcpy(p, &msize, sizeof(msize));
//	p += sizeof(msize);
	
	memcpy(p, &rsize, sizeof(rsize));
	p += sizeof(rsize);
	
	// put in the data

	memcpy(p, gbuf, gsize);
	p += gsize;


//	memcpy(p, mbuf, msize);
//	p += msize;
	
	memcpy(p, rbuf, rsize);
	p += rsize;
	
	delete [] gbuf;
//	delete [] mbuf;
	delete [] rbuf;
	
	return size;
}

int GamlConfig::Deserialize(char* buf, int size)	{
	int gsize, msize, rsize;
	
	char* p = buf;
	
	memcpy(&gsize, p, sizeof(gsize));
	p += sizeof(gsize);
*/
/*	
	memcpy(&msize, p, sizeof(msize));
	p += sizeof(msize);
*/	
/*	memcpy(&rsize, p, sizeof(rsize));
	p += sizeof(rsize);
	
	gc.Deserialize(p, gsize);
	p += gsize;
	rc.Deserialize(p, rsize);
	p += rsize;
	
	// sanity checks
	assert(p-buf == size);
	
	return 0;
}

bool GamlConfig::operator==(const GamlConfig& rhs) const	{
	if (	gc == rhs.gc	&&
			mc == rhs.mc	&&
			rc == rhs.rc
		)
		return true;
	return false;
}
*/

