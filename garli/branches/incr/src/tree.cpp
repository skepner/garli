// GARLI version 0.96b8 source code
// Copyright 2005-2008 Derrick J. Zwickl
// email: zwickl@nescent.org
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//	NOTE: Portions of this source adapted from GAML source, written by Paul O. Lewis

#include <algorithm>
#include <vector>
#include <list>
#include <cassert>
#ifdef UNIX
	#include <sys/mman.h>
#endif

using namespace std;

#include "defs.h"
#include "sequencedata.h"
#include "clamanager.h"
#include "funcs.h"
#include "stopwatch.h"
#include "model.h"
#include "tree.h"
#include "reconnode.h"
#include "garlireader.h"

#include "utility.h"

#ifdef CUDA_GPU
#include "cudaman.h"
#endif

Profiler ProfIntInt   ("ClaIntInt     ");
Profiler ProfIntTerm  ("ClaIntTerm    ");
Profiler ProfTermTerm ("ClaTermTerm   ");
Profiler ProfRescale  ("Rescale       ");
Profiler ProfScoreInt ("ScoreInt      ");
Profiler ProfScoreTerm("ScoreTerm     ");
Profiler ProfEQVectors("EQVectors     ");

/*
FLOAT_TYPE precalcThresh[30];
FLOAT_TYPE precalcMult[30];
int precalcIncr[30] = {1, 3, 5, 7, 10, 12, 14, 17, 19, 21, 24, 26, 28, 30, 33, 35, 37, 40, 42, 44, 47, 49, 51, 53, 56, 58, 60, 63, 65, 67};
*/

extern rng rnd;
extern bool output_tree;
extern bool uniqueSwapTried;

#ifdef VARIABLE_OPTIMIZATION
ofstream var("variable.log");
ofstream uni("unique.log");
#endif

#ifdef OUTPUT_UNIQUE_TREES
ofstream uni("unique.log");
#endif

//external global variables
extern int calcCount;
extern int optCalcs;
extern ofstream opt;
extern ofstream optsum;
extern int memLevel;
extern ModelSpecification modSpec;

#ifdef CUDA_GPU
extern CudaManager *cudaman;
#endif

//Tree static definitions
FLOAT_TYPE Tree::meanBrlenMuts;
FLOAT_TYPE Tree::alpha;
FLOAT_TYPE Tree::min_brlen;	  // branch lengths never below this value
FLOAT_TYPE Tree::max_brlen;
FLOAT_TYPE Tree::exp_starting_brlen;    // expected starting branch length
ClaManager *Tree::claMan;
list<TreeNode *> Tree::nodeOptVector;
const SequenceData *Tree::data;
unsigned Tree::rescaleEvery;
FLOAT_TYPE Tree::rescaleBelow;
FLOAT_TYPE Tree::treeRejectionThreshold;
std::vector<Constraint> Tree::constraintsVec;
AttemptedSwapList Tree::attemptedSwaps;
FLOAT_TYPE Tree::uniqueSwapBias;
FLOAT_TYPE Tree::distanceSwapBias;
FLOAT_TYPE Tree::expectedPrecision;

FLOAT_TYPE Tree::uniqueSwapPrecalc[500];
FLOAT_TYPE Tree::distanceSwapPrecalc[1000];

//DEBUG
//FLOAT_TYPE Tree::rescalePrecalcThresh[30];
//FLOAT_TYPE Tree::rescalePrecalcMult[30];
//int Tree::rescalePrecalcIncr[30];

FLOAT_TYPE Tree::rescalePrecalcThresh[RESCALE_ARRAY_LENGTH];
FLOAT_TYPE Tree::rescalePrecalcMult[RESCALE_ARRAY_LENGTH];
int Tree::rescalePrecalcIncr[RESCALE_ARRAY_LENGTH];

Bipartition *Tree::outgroup = NULL;

int Tree::siteToScore = -1;

void InferStatesFromCla(char *states, FLOAT_TYPE *cla, int nchar);
FLOAT_TYPE CalculateHammingDistance(const char *str1, const char *str2, int nchar);
void SampleBranchLengthCurve(FLOAT_TYPE (*func)(TreeNode*, Tree*, FLOAT_TYPE, bool), TreeNode *thisnode, Tree *thistree);
FLOAT_TYPE CalculatePDistance(const char *str1, const char *str2, int nchar);
inline FLOAT_TYPE CallBranchLike(TreeNode *thisnode, Tree *thistree, FLOAT_TYPE blen, bool brak);



void Tree::CopyClaIndeces(const Tree *from, bool remove){
	//the bool argument "remove" designates whether the tree currently has cla arrays
	//assigned to it or not (if not, it must have come from the unused tree vector)

	//do the clas down
	if((allNodes[0]->claIndexDown != -1) && remove)
		claMan->DecrementCla(allNodes[0]->claIndexDown);
	allNodes[0]->claIndexDown=from->allNodes[0]->claIndexDown;
	if(allNodes[0]->claIndexDown != -1)
		claMan->IncrementCla(allNodes[0]->claIndexDown);
	
#ifdef EQUIV_CALCS
	if(from->dirtyEQ == false){
		memcpy(allNodes[0]->tipData, from->allNodes[0]->tipData, data->NChar()*sizeof(char));
		for(int i=numTipsTotal+1;i<allNodes.size();i++)
			memcpy(allNodes[i]->tipData, from->allNodes[i]->tipData, data->NChar()*sizeof(char));
		dirtyEQ = false;
		}
	else dirtyEQ = true;
#endif

	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		if(remove && (allNodes[i]->claIndexDown != -1))
			claMan->DecrementCla(allNodes[i]->claIndexDown);
		allNodes[i]->claIndexDown=from->allNodes[i]->claIndexDown;
		if(allNodes[i]->claIndexDown != -1)
			claMan->IncrementCla(allNodes[i]->claIndexDown);
		}
		
	//do the clas up left
	if(remove && (allNodes[0]->claIndexUL != -1))
		claMan->DecrementCla(allNodes[0]->claIndexUL);
	allNodes[0]->claIndexUL=from->allNodes[0]->claIndexUL;
	if(allNodes[0]->claIndexUL != -1)
		claMan->IncrementCla(allNodes[0]->claIndexUL);
	
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		if(remove && (allNodes[i]->claIndexUL != -1))
			claMan->DecrementCla(allNodes[i]->claIndexUL);
		allNodes[i]->claIndexUL=from->allNodes[i]->claIndexUL;
		if(allNodes[i]->claIndexUL != -1)
			claMan->IncrementCla(allNodes[i]->claIndexUL);
		}
	
	//do the clas up right
	if(remove && (allNodes[0]->claIndexUR != -1))
		claMan->DecrementCla(allNodes[0]->claIndexUR);
	allNodes[0]->claIndexUR=from->allNodes[0]->claIndexUR;
	if(allNodes[0]->claIndexUR != -1)
		claMan->IncrementCla(allNodes[0]->claIndexUR);
		
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		if(remove && (allNodes[i]->claIndexUR != -1))
			claMan->DecrementCla(allNodes[i]->claIndexUR);
		allNodes[i]->claIndexUR=from->allNodes[i]->claIndexUR;
		if(allNodes[i]->claIndexUR != -1)
			claMan->IncrementCla(allNodes[i]->claIndexUR);
		}
	}

void Tree::RemoveTreeFromAllClas(){
	if(root->claIndexDown != -1){
		claMan->DecrementCla(root->claIndexDown);
		root->claIndexDown=-1;
		}
	if(root->claIndexUL != -1){
		claMan->DecrementCla(root->claIndexUL);
		root->claIndexUL=-1;
		}
	if(root->claIndexUR != -1){	
		claMan->DecrementCla(root->claIndexUR);
		root->claIndexUR=-1;
		}
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		if(allNodes[i]->claIndexDown != -1){
			claMan->DecrementCla(allNodes[i]->claIndexDown);
			allNodes[i]->claIndexDown=-1;
			}
		if(allNodes[i]->claIndexUL != -1){
			claMan->DecrementCla(allNodes[i]->claIndexUL);
			allNodes[i]->claIndexUL=-1;
			}
		if(allNodes[i]->claIndexUR != -1){
			claMan->DecrementCla(allNodes[i]->claIndexUR);
			allNodes[i]->claIndexUR=-1;
			}
		}
	}

//basic function to deal with the odd data string format that I use for nuc data
const char *AdvanceDataPointer(const char *arr, int num){
	for(int a=0;a<num;a++){
		if(*arr > -1 || *arr == -4) arr++;
		else{
			int states = -1 * *arr;
			do{
				arr++;
				}while (states-- > 0);
			}
		}
	return arr;
	}

void Tree::SetTreeStatics(ClaManager *claMan, const SequenceData *data, const GeneralGamlConfig *conf){
	Tree::claMan=claMan;
	Tree::data=data;
#ifdef SINGLE_PRECISION_FLOATS
	Tree::rescaleEvery = 6;
	Tree::rescaleBelow = exp(-1.0f); //this is 0.368
	for(int i=0;i<30;i++){
		Tree::rescalePrecalcIncr[i] = i*3 - (int) log(rescaleBelow);
		Tree::rescalePrecalcThresh[i] = exp((FLOAT_TYPE)(-rescalePrecalcIncr[i]));
		Tree::rescalePrecalcMult[i] =  exp((FLOAT_TYPE)(rescalePrecalcIncr[i]));
		}

	FLOAT_TYPE minVal = 1.0e-10f;
	FLOAT_TYPE maxVal = 1.0e10f;
#else
	Tree::rescaleEvery=16;
	Tree::rescaleBelow = exp(-24.0); //this is 1.026e-10
	for(int i=0;i<RESCALE_ARRAY_LENGTH;i++){
		Tree::rescalePrecalcIncr[i] = i*7 - (int) log(rescaleBelow);
		Tree::rescalePrecalcThresh[i] = exp((FLOAT_TYPE)(-rescalePrecalcIncr[i]));
		Tree::rescalePrecalcMult[i] =  exp((FLOAT_TYPE)(rescalePrecalcIncr[i]));
		}

	FLOAT_TYPE minVal = 1.0e-20;
	FLOAT_TYPE maxVal = 1.0e20;
#endif
	Tree::uniqueSwapBias = conf->uniqueSwapBias;
	Tree::distanceSwapBias = conf->distanceSwapBias;
	for(int i=0;i<500;i++){
		Tree::uniqueSwapPrecalc[i] = (FLOAT_TYPE) pow(Tree::uniqueSwapBias, i);
		//if(Tree::uniqueSwapPrecalc[i] != Tree::uniqueSwapPrecalc[i]) Tree::uniqueSwapPrecalc[i]=0.0f;
		if(Tree::uniqueSwapPrecalc[i] < minVal) Tree::uniqueSwapPrecalc[i] = minVal;
		if(Tree::uniqueSwapPrecalc[i] > maxVal) Tree::uniqueSwapPrecalc[i] = maxVal;
		}
	for(int i=0;i<1000;i++){
		Tree::distanceSwapPrecalc[i] = (FLOAT_TYPE) pow(Tree::distanceSwapBias, i);
		//if(Tree::distanceSwapPrecalc[i] != Tree::distanceSwapPrecalc[i]) Tree::distanceSwapPrecalc[i]=0.0f;
		if(Tree::distanceSwapPrecalc[i] < minVal) Tree::distanceSwapPrecalc[i] = minVal;
		if(Tree::distanceSwapPrecalc[i] > maxVal) Tree::distanceSwapPrecalc[i] = maxVal;
		}

	Tree::meanBrlenMuts	= conf->meanBrlenMuts;
	Tree::alpha		= conf->gammaShapeBrlen;
	Tree::treeRejectionThreshold = conf->treeRejectionThreshold;
	Tree::min_brlen = conf->minBrlen;
	Tree::max_brlen = conf->maxBrlen;
	Tree::exp_starting_brlen = conf->startingBrlen;

	//deal with the outgroup specification, if there is one
	if(conf->outgroupString.length() > 0){
		if(outgroup)
			outgroup->Clear();
		else
			outgroup = new Bipartition();

		GarliReader &reader = GarliReader::GetInstance();
		if(reader.GetTaxaBlock(0)->GetNTax() > 0){
			//now using NCL to much more rigorously and flexibly read the outgroup specification
			NxsString tax(conf->outgroupString.c_str());
			tax += ";";
			std::istringstream s(tax);
			NxsToken tok(s);
			tok.GetNextToken();
			NxsUnsignedSet iset;
			try{
				NxsSetReader::ReadSetDefinition(tok, *reader.GetTaxaBlock(0), "outgroup", "GARLI configuration", &iset);
				outman.UserMessage("Found outgroup specification: %s\n", NxsSetReader::GetSetAsNexusString(iset).c_str());
				}
			catch (const NxsException & x){
				throw ErrorException("%s", x.msg.c_str());
				}

			//the set has been read as indeces, so change to taxon numbers before passing to the bipart func
			NxsUnsignedSet nset;
			for(NxsUnsignedSet::const_iterator it = iset.begin();it != iset.end(); it++)
				nset.insert(*it + 1);

			outgroup->BipartFromNodenums(nset);
			}
		else{//the old half-assed outgroup reader
			vector<int> nums;
			unsigned pos1=0, pos2;
			while(pos1 < conf->outgroupString.size()){
				pos2 = conf->outgroupString.find(" ", pos1+1);
				string tax = conf->outgroupString.substr(pos1, pos2 - pos1);
				tax = NxsString::strip_whitespace(tax);
				for(string::iterator it = tax.begin();it != tax.end();it++)
					if(isdigit(*it) == false)
						throw ErrorException("problem in outgroup specification.\nExpecting taxon numbers separated by spaces, found %s.", tax.c_str());
				nums.push_back(atoi(tax.c_str()));
				pos1 = pos2;
				}
			outman.UserMessageNoCR("Found outgroup specification: ");
			for(vector<int>::iterator it = nums.begin();it != nums.end();it++)
				outman.UserMessageNoCR("%d ", *it);
			outman.UserMessage("\n");
			outgroup->BipartFromNodenums(nums);
			}
		}
	}

//this assumes that a tree string has been passed in with *s pointing to the first char of a blen
//description, and reads and advances the string up to the next non-blen character.  The string that
//was interpreted as the branch length is placed into the NxsString passed in
double ReadBranchlength(const char *&s, NxsString &blen){
	blen = "";
	while(*(s+1) && *(s+1)!=')'&& *(s+1)!=',' && *(s+1)!=';'){
		blen += *(s+1);
		s++;
		}
	s++;
	double len;
	if(NxsString::to_double(blen.c_str(), &len) == false)
		throw ErrorException("Problem reading tree description.  Illegal branch-length specification: \"%s\"", blen.c_str());
	return len;
	}

//DJZ 4-28-04
//adding the ability to read in treestrings in which the internal node numbers are specified.  I'd like to make the
//internal numbers be specified the way that internal node labels are according to the newick format, ie directly after
//the closing paren that represents the internal node.  But, that makes going from string -> tree annoying
//because by the time the internal node number would be read the treeNode structure would have already been created.

//So, the internal node numbers will go just BEFORE the opening paren that represents that node
//Example:  50(1:.05, 2:.02):.1 signifies a node numbered 50 that is ancestral to 1 and 2.
Tree::Tree(const char* s, bool numericalTaxa, bool allowPolytomies /*=false*/, bool allowMissingTaxa /*=false*/){
	//if we are using this constructor, we can't guarantee that the tree will be specified unrooted (with
	//a trifurcating root), so use an allocation function that is guaranteed to have enough room and then
	//trifurcate and delete if necessary
	AllocateTree();
	TreeNode *temp=root;
	root->SetAttached(true);
	int current=numTipsTotal+1;
	bool cont=false;
	this->numBranchesAdded = 0;
	while(*s){
		cont = false;
		if(*s == ';')
			break;  // ignore semicolons
		else if(*s == ' ' || *s == '\t')
			s++;
			//DEBUG
			//break;  // ignore spaces
		else if(*s == ')'){
			//we're closing a paren, moving a node toward the root
			assert(temp->anc);
			if(!temp->anc) throw ErrorException("Problem reading tree description.  Mismatched parentheses?");
			temp=temp->anc;
			s++;
			//while(*s && !isgraph(*s))
			//an internal node label might appear here, so ignore anything up to one of these valid next characters
			while(*s && (*s != ',') && (*s != ':') && (*s != ',') && (*s != ')') && (*s != ';'))
				s++;
			if(*s==':'){//adding a branch length
				NxsString len;
				temp->dlen = ReadBranchlength(s, len);
				if(temp->dlen < min_brlen){
					outman.UserMessage("->Branch of length %s is less than min of %.1e.  Setting to min.", len.c_str(), min_brlen);
					temp->dlen = min_brlen;
					}
				else if (temp->dlen > max_brlen){
					outman.UserMessage("->Branch of length %s is greater than max of %.0f.  Setting to max.", len.c_str(), max_brlen);
					temp->dlen = max_brlen;
					}
				}
			else if(*s==','||*s==')'){
				temp->dlen=Tree::exp_starting_brlen;
#ifdef STOCHASTIC_STARTING_BLENS
				temp->dlen *= rnd.gamma(1.0);
#endif
				}
			else
				{
				if(*s==';'){
					s++;
					while(*s){
						if(*s != ' ' && *s != '\t')
							outman.UserMessage("Warning: extraneous character (%c) found after ; in tree description", *s);
						s++;
						}
					break;
					}
				else if(*s == ' ' || *s == '\t') s++;
				else if(*s == '\0' || *s == '\n' || *s == '\r') break;
				else throw ErrorException("Unexpected character found in tree description at this point: %s", s);
	//			assert(!*s  || *s==';');
				}
			}
		else if(*s == ','){
			assert(temp->anc);
			if(!temp->anc) throw ErrorException("Problem reading tree description.  Mismatched parentheses?");
			temp=temp->anc;
			if(*(s+1)!='(') {
				s++;
			}
			else {
				numBranchesAdded--; // this ( will be encountered in two consecutive loops, so we decrement numBranchesAdded to avoid overcounting.
			}
			cont = true;
			}
		if(*s == '(' || isdigit(*s) || cont==true){
			//here we're about to add a node of some sort
			this->numBranchesAdded++;
			if(*(s+1)=='('){//add an internal node
				temp=temp->AddDes(allNodes[current++]);
				numNodesAdded++;
				s++;
				}
			else{
				//this gets ugly.  At this point we could be adding an internal node with the internal node
				//num specifed, or a terminal node.  Either way the next characters in the string will be
				//digits.  We'll have to look ahead to see what the next non-digit character is.  If it's
				//a '(', we know we are adding a prenumbered internal
				if(*s=='(') {
					this->numBranchesAdded++;
					s++;
				}
				int i=0;
				bool term=true;
				while(isdigit(*(s+i))) i++;
				if(*(s+i) == '(') term=false;

				if(term == false){//add an internal node with the nodenum specified in the string
					NxsString num;
					num = *s;
					while(isdigit(*(s+1))){
						assert(*s);
						num += *++s;
						}
					int internalnodeNum = atoi( num.c_str() );
	                temp=temp->AddDes(allNodes[internalnodeNum]);
	                numNodesAdded++;
	                s++;
					}
				else{//add a terminal node
					// read taxon name
	                 NxsString name;
	                 name = *s;
	                 int taxonnodeNum;
					if(numericalTaxa==true){
						while(isdigit(*(s+1))){
							assert(*s);
							name += *++s;
							}
						taxonnodeNum = atoi( name.c_str() );
						if(taxonnodeNum == 0) throw ErrorException("Unexpected character(s) found in tree description \"%s!\"", name.c_str());
						if(taxonnodeNum > numTipsTotal) throw ErrorException("Taxon number in tree description (%d) is greater than\n\tnumber of taxa in dataset!", taxonnodeNum);
						}
					else{
						while(*(s+1) != ':' && *(s+1) != ',' && *(s+1) != ')'){
							assert(*s);
							name += *++s;
							}
						//This is a bit annoying.  If the tree string came directly from NCL then GetEscaped should get any
						//names to match the names present in the datamatrix (whether Nexus or not).  But, if the tree string
						//came from a start file with just a newick string there are various possibilities.  First try interpreting
						//the name as-is.  If that doesn't work, try GetEscaped.  If that doesn't work, try removing quotes (if any)
						//before calling GetEscaped
						taxonnodeNum = data->TaxonNameToNumber(name);
						if(taxonnodeNum < 0){
							NxsString esc = NxsString::GetEscaped(name).c_str();
							taxonnodeNum = data->TaxonNameToNumber(esc);
							}
						if(taxonnodeNum < 0){
							if(name.c_str()[0] == '\'' && name.c_str()[name.size()-1] == '\''){
								NxsString esc2;
								for(int c=1;c<name.size()-1;c++){
									esc2 += name[c];
									}
								NxsString esc = NxsString::GetEscaped(esc2).c_str();
								taxonnodeNum = data->TaxonNameToNumber(esc);
								}
							}
						if(taxonnodeNum < 0){
							throw ErrorException("Unknown taxon \"%s\" encountered in tree description!", name.c_str());
							}
						}
	                temp=temp->AddDes(allNodes[taxonnodeNum]);
	                numNodesAdded++;
					numTipsAdded++;
	                s++;
					while(*s == ' ') s++;;//eat any spaces here

					if(*s!=':' && *s!=',' && *s!=')'){
						throw ErrorException("Problem parsing tree string!  Expecting \":\" or \",\" or \")\", found %c", *s);
						s--;
						ofstream str("treestring.log", ios::app);
						str << s << endl;
						str.close();
						assert(0);
						}

	                if(*s==':'){
						NxsString len;
						temp->dlen = ReadBranchlength(s, len);
						if(temp->dlen < min_brlen){
							outman.UserMessage("->Branch of length %s is less than min of %.1e.  Setting to min.", len.c_str(), min_brlen);
							temp->dlen = min_brlen;
							}
						else if (temp->dlen > max_brlen){
							outman.UserMessage("->Branch of length %s is greater than max of %.0f.  Setting to max.", len.c_str(), max_brlen);
							temp->dlen = max_brlen;
							}
						}
					else{
						temp->dlen = Tree::exp_starting_brlen;
#ifdef STOCHASTIC_STARTING_BLENS
						temp->dlen *= rnd.gamma(1.0);
#endif
						}
					}
				}
			}
		}
	this->numBranchesAdded--; // now we reduce the count of branches because the outer () count as a branch, but we don't really use the branch leading to the root of the tree

	if((allowMissingTaxa == false) && (numTipsAdded != numTipsTotal))
		throw ErrorException("Number of taxa in tree description (%d) not equal to number of\n\ttaxa in dataset (%d)!  Check tree string.", numTipsAdded, numTipsTotal);
		
	if(root->left->next==root->right){
		MakeTrifurcatingRoot(true, false);
		}
	else	{
		EliminateNode(2*data->NTax()-2);
		}
	assert(root->left->next!=root->right);

	root->CheckforLeftandRight();
	if(allowPolytomies == false) {
		root->CheckforPolytomies();
		}
	root->CheckTreeFormation();
	bipartCond = DIRTY;
	}

//DZ 10-31-02
//separating general tree construction stuff from CLA assignment/allocation
//which will happend differently if the CLAs are shared or not
Tree::Tree(){
	const unsigned nNodes = 2*data->NTax() - 2;
	allNodes.assign(nNodes, (TreeNode *) 0L);
	for(int i=0;i<nNodes;i++){
		allNodes[i]=new TreeNode(i);
		}
	root=allNodes[0];
	root->SetAttached(true);
	//assign data to tips
	for(int i=1;i<=data->NTax();i++){
		if(modSpec.IsNucleotide()){
			allNodes[i]->tipData=static_cast<const NucleotideData *>(data)->GetAmbigString(i-1);
#ifdef OPEN_MP
			allNodes[i]->ambigMap=static_cast<const NucleotideData *>(data)->GetAmbigToCharMap(i-1);
#endif
			}
		else
			allNodes[i]->tipData=(char *)(data)->GetRow(i-1);
		}

	numTipsAdded=0;
	numNodesAdded=1;//root
	numTipsTotal=data->NTax();
	lnL=0.0;

	calcs=0;
	numBranchesAdded=0;
	taxtags=new int[numTipsTotal+1];
	bipartCond = DIRTY;

#ifdef EQUIV_CALCS
	//need to do the root too, since that node is sometimes stolen
	allNodes[0]->tipData = new char[data->NChar()];
	for(int i=data->NTax()+1;i<allNodes.size();i++){
		allNodes[i]->tipData = new char[data->NChar()];
		}
	dirtyEQ=true;
#endif
	}

void Tree::AllocateTree(){
	calcs=0;
	const unsigned nNodes = 2*data->NTax() - 1;
	allNodes.assign(nNodes, (TreeNode *) 0L);
	for(int i=0;i<nNodes;i++){
		allNodes[i]=new TreeNode(i);
		}
	root=allNodes[0];
	for(int i=1;i<=data->NTax();i++){
		if(modSpec.IsNucleotide()){
			allNodes[i]->tipData=static_cast<const NucleotideData *>(data)->GetAmbigString(i-1);
#ifdef OPEN_MP
			allNodes[i]->ambigMap=static_cast<const NucleotideData *>(data)->GetAmbigToCharMap(i-1);
#endif
			}
		else
			allNodes[i]->tipData=(char *)(data)->GetRow(i-1);
		}

	numTipsAdded=0;
	numNodesAdded=1;//root
	numTipsTotal=data->NTax();
	lnL=0.0;
	numBranchesAdded=0;
	taxtags=new int[numTipsTotal+1];
	}

Tree::~Tree(){
	if(taxtags!=NULL)
		delete []taxtags;
	for(int x=0; x<allNodes.size(); x++)
		delete allNodes[x];
	}

int Tree::BrlenMutate(){
	//random_binomial is now called with the mean number of blen muts, which is easiser to specify across datasets
	//than is a per branch probability
	int numBrlenMuts;
	if(rnd.uniform() < 0.05){//do a whole tree rescale occasionally
		ScaleWholeTree();
		numBrlenMuts = allNodes.size() - 1;
		}
	else{
		do{
			numBrlenMuts=rnd.random_binomial(allNodes.size() - 1, meanBrlenMuts);
			}while(numBrlenMuts==0);
		for(int i=0;i<numBrlenMuts;i++){
			int branch=GetRandomNonRootNode();
			allNodes[branch]->dlen*=rnd.gamma( Tree::alpha );
			SweepDirtynessOverTree(allNodes[branch]);
			allNodes[branch]->dlen = (allNodes[branch]->dlen > min_brlen ? (allNodes[branch]->dlen < max_brlen ? allNodes[branch]->dlen : max_brlen) : min_brlen);
			}
		}
	return numBrlenMuts;
	}

void Tree::PerturbAllBranches(){
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		allNodes[i]->dlen*=rnd.gamma(100);
		}
	MakeAllNodesDirty();
	}

void Tree::RandomizeBranchLengths(FLOAT_TYPE lowLimit, FLOAT_TYPE highLimit){
	FLOAT_TYPE range = (highLimit - lowLimit);
	for(int i=1;i<allNodes.size();i++){
		allNodes[i]->dlen = lowLimit + (rnd.uniform() * range);
		}
	MakeAllNodesDirty();
	}

void Tree::RandomizeBranchLengthsExponential(FLOAT_TYPE lambda){

	for(int i=1;i<allNodes.size();i++){
		allNodes[i]->dlen = rnd.exponential(lambda);
		}
/*
	FLOAT_TYPE low = log(lowLimit);
	FLOAT_TYPE high = log(highLimit);
	FLOAT_TYPE range = high - low;
	for(int i=1;i<allNodes.size();i++){
		allNodes[i]->dlen = exp(low + rnd.uniform() * range);
		}
*/
	MakeAllNodesDirty();
	}

void Tree::ScaleWholeTree(FLOAT_TYPE factor/*=-1.0*/){
	if(factor==-1.0) factor = rnd.gamma( Tree::alpha );
	//9-12-06 Stupid!  Why the hell was this only scaling the internals?
	//for(int i=numTipsTotal;i<allNodes.size();i++){
	for(int i=1;i<allNodes.size();i++){
		allNodes[i]->dlen*=factor;
		allNodes[i]->dlen = (allNodes[i]->dlen > min_brlen ? (allNodes[i]->dlen < max_brlen ? allNodes[i]->dlen : max_brlen) : min_brlen);
		assert(!(allNodes[i]->dlen < min_brlen));
		}
	MakeAllNodesDirty();
	lnL=-ONE_POINT_ZERO;
	}

int Tree::BrlenMutateSubset(vector<int> const &subtreeMemberNodes){
	int numBrlenMuts;
	do{
		numBrlenMuts=rnd.random_binomial((int)subtreeMemberNodes.size(), meanBrlenMuts);
		}while(numBrlenMuts==0);
	for(int i=0;i<numBrlenMuts;i++){
		int branch=subtreeMemberNodes[(int)(rnd.uniform()*subtreeMemberNodes.size())];//can't mutate the root
		allNodes[branch]->dlen*=rnd.gamma( Tree::alpha );
		SweepDirtynessOverTree(allNodes[branch]);
		allNodes[branch]->dlen = (allNodes[branch]->dlen > min_brlen ? (allNodes[branch]->dlen < max_brlen ? allNodes[branch]->dlen : max_brlen) : min_brlen);
		}
	return numBrlenMuts;
	}

void Tree::MakeTrifurcatingRoot(bool reducenodes, bool clasAssigned ){
	//reducenodes should only =1 if this function is called after generating a random tree
	//or after reading in a tree with a bifurcating root.  DO NOT call with reducenodes=1 if
	//this is being used after one of the initial root branches was pruned off

	//clasAssigned should be true if the clas have been assigned to the nodes by the claManager.
	//(ie, not right after tree creation)
	TreeNode *t1, *removedNode;
	vector<TreeNode *> rootDesc;
	assert(root->left->next==root->right);

	if(root->left->IsInternal()){
		removedNode = root->left;
		root->right->dlen += removedNode->dlen;
		rootDesc.push_back(root->right);
		}
	else{
		removedNode = root->right;
		root->left->dlen += removedNode->dlen;
		rootDesc.push_back(root->left);
		}
	if(clasAssigned){
		removedNode->claIndexDown=claMan->SetDirty(removedNode->claIndexDown);
		removedNode->claIndexUL=claMan->SetDirty(removedNode->claIndexUL);
		removedNode->claIndexUR=claMan->SetDirty(removedNode->claIndexUR);
		}
	t1 = removedNode->left;
	while(t1){
		rootDesc.push_back(t1);
		t1 = t1->next;
		}
	//now we have all of the new desc of the root
	//disconnect the old ones
	root->left = root->right = NULL;
	for(unsigned t=0;t<rootDesc.size();t++)
		root->AddDes(rootDesc[t]);

/*
	if(root->left->IsInternal()){
		removedNode=root->left;
		t1=root->left->left;
		t2=root->left->right;
		l=root->left->dlen;
		root->right->dlen+=l;
		root->left->SetAttached(false);
		if(clasAssigned){
			root->left->claIndexDown=claMan->SetDirty(root->left->claIndexDown);
			root->left->claIndexUL=claMan->SetDirty(root->left->claIndexUL);
			root->left->claIndexUR=claMan->SetDirty(root->left->claIndexUR);
			}
		root->left=t1;
		t1->next=t2;
		t2->prev=t1;
		t2->next=root->right;
		root->right->prev=t2;
		t1->anc=root;
		t2->anc=root;
		}
	else {
		removedNode=root->right;
		t1=root->right->left;
		t2=root->right->right;
		l=root->right->dlen;
		root->left->dlen+=l;
	 	root->right->SetAttached(false);
		if(clasAssigned){
			root->right->claIndexDown=claMan->SetDirty(root->right->claIndexDown);
			root->right->claIndexUL=claMan->SetDirty(root->right->claIndexUL);
			root->right->claIndexUR=claMan->SetDirty(root->right->claIndexUR);
			}
	 	root->left->next=t1;
	 	t1->prev=root->left;
		t1->next=t2;
		t2->prev=t1;
		t2->next=NULL;
		t1->anc=root;
		t2->anc=root;
	 	root->right=t2;
		}
*/	if(reducenodes==1){
		//we need to permanently get rid of the node that was removed and decrement the nodeNums of those greater
		//than it.
		SortAllNodesArray();
		EliminateNode(removedNode->nodeNum);
		numBranchesAdded--;
		numNodesAdded--;
		}
	}

bool Tree::ArbitrarilyBifurcate(){
	//note that this assumes that the root has been already been made into at least a trichotomy
	if(numNodesAdded == allNodes.size())
		return false;
	assert (numNodesAdded < allNodes.size());
	//first figure out which internal nodenums haven't been used yet
	int placeInAllNodes=1;
	while(allNodes[placeInAllNodes]->IsAttached()) {
		placeInAllNodes++;
		}
	vector<TreeNode*> nodes;
	TreeNode *curNode = root;
	TreeNode *desNode;
	bool goingDown = false;
	bool polytomiesFound = false;

	while(numNodesAdded < allNodes.size()){
		if(curNode->IsInternal() && !goingDown){
			desNode = curNode->left;
			nodes.push_back(desNode);
			while(desNode->next){
				desNode = desNode->next;
				nodes.push_back(desNode);
				}
			if((curNode != root && nodes.size() > 2) || (curNode == root && nodes.size() > 3)){
				polytomiesFound = true;
				bipartCond = DIRTY;
				int first = rnd.random_int(nodes.size());
				int second;
				do{
					second = rnd.random_int(nodes.size());
					}while(first == second);
				TreeNode *move1 = nodes[first];
				TreeNode *move2 = nodes[second];
				TreeNode *nextInternal = allNodes[placeInAllNodes];

				curNode->RemoveDes(move1);
				curNode->RemoveDes(move2);
				nextInternal->AddDes(move1);
				nextInternal->AddDes(move2);
				curNode->AddDes(nextInternal);

				nextInternal->dlen=Tree::exp_starting_brlen;
#ifdef STOCHASTIC_STARTING_BLENS
				nextInternal->dlen *= rnd.gamma(1.0);
#endif
				placeInAllNodes++;
				numNodesAdded++;
				}
			else{
				if(curNode->left && !goingDown){
					curNode = curNode->left;
					}
				else if(curNode->next){
					curNode = curNode->next;
					goingDown = false;
					}
				else{
					curNode = curNode->anc;
					goingDown = true;
					}
				}
			}
		else{
			if(curNode->next){
				curNode = curNode->next;
				goingDown = false;
				}
			else{
				curNode = curNode->anc;
				goingDown = true;
				}
			}

		nodes.clear();
		}
	assert(numNodesAdded == allNodes.size());
	return polytomiesFound;
	}

void Tree::AddRandomNode(int nodenum , int &placeInAllNodes){

	assert(nodenum>0 && nodenum<=numTipsTotal);  //should be adding a terminal
	TreeNode* nd=allNodes[nodenum];
	nd->dlen = Tree::exp_starting_brlen;
#ifdef STOCHASTIC_STARTING_BLENS
	nd->dlen *= rnd.gamma(1.0);
#endif
	if(nd->dlen < min_brlen) nd->dlen = min_brlen;
	else if(nd->dlen > max_brlen) nd->dlen = max_brlen;

	nd->next=nd->prev=NULL;//in case this node was connected in some other tree

	//Make sure that the root has 3 decendents
	if(numBranchesAdded<3)
		{root->AddDes(nd);
		}
	else
		{// If we're not adding directly to the root node, then we will need
		// a connector node and make the new terminal its left des
		TreeNode* connector=allNodes[placeInAllNodes++];
		numNodesAdded++;
		connector->dlen = Tree::exp_starting_brlen;
#ifdef STOCHASTIC_STARTING_BLENS
		connector->dlen *= rnd.gamma(1.0);
#endif
		nd->dlen = (nd->dlen > min_brlen ? nd->dlen : min_brlen);
		connector->left=connector->right=NULL;
		connector->AddDes(nd);

		//select a branch to break with the connector
		int k = rnd.random_int( numBranchesAdded ) + 1;
		TreeNode* otherDes = root->FindNode( k );

		// replace puts connection in the tree where otherDes had been
		otherDes->SubstituteNodeWithRespectToAnc(connector);

		//add otherDes back to the tree as the sister to the new tip
		connector->AddDes(otherDes);
		numBranchesAdded++;//numBranchesAdded needs to be incremented twice because a total of two branches have been added
		}
	numBranchesAdded++;
	numNodesAdded++;
	numTipsAdded++;
	bipartCond = DIRTY;
	}

void Tree::AddRandomNodeWithConstraints(int nodenum, int &placeInAllNodes, Bipartition *mask){
	//the trick here with the constraints is that only a subset of the taxa will be in the
	//growing tree.  To properly determine bipartition comptability a mask consisting of only
	//the present taxa will need to be used

	assert(nodenum>0 && nodenum<=numTipsTotal);  //should be adding a terminal
	TreeNode* nd=allNodes[nodenum];
	Bipartition temp;
	*mask += temp.TerminalBipart(nodenum);
	nd->dlen = Tree::exp_starting_brlen;
#ifdef STOCHASTIC_STARTING_BLENS
	nd->dlen *= rnd.gamma(1.0);
#endif
	if(nd->dlen < min_brlen) nd->dlen = min_brlen;
	else if(nd->dlen > max_brlen) nd->dlen = max_brlen;

	nd->next=nd->prev=NULL;//in case this node was connected in some other tree

	//Make sure that the root has 3 decendents
	if(numBranchesAdded<3)
		{root->AddDes(nd);
		}
	else
		{// If we're not adding directly to the root node, then we will need
		// a connector node and make the new terminal its left des
		TreeNode* connector=allNodes[placeInAllNodes++];
		numNodesAdded++;
		connector->dlen = Tree::exp_starting_brlen;
#ifdef STOCHASTIC_STARTING_BLENS
		connector->dlen *= rnd.gamma(1.0);
#endif
		connector->dlen = (connector->dlen > min_brlen ? connector->dlen : min_brlen);
		connector->left=connector->right=NULL;
		connector->AddDes(nd);

		//select a branch to break with the connector
		int k;
		TreeNode *otherDes;
		bool compat;
		Bipartition proposed;
		nd->CalcBipartition(false);

		do{
			k = rnd.random_int( numBranchesAdded ) + 1;
			otherDes = root->FindNode( k );
			compat=true;
			CalcBipartitions(true);
			proposed.FillWithXORComplement(nd->bipart, otherDes->bipart);
			vector<Constraint>::const_iterator conit = Tree::GetConstraints().begin();
			for(;conit != Tree::GetConstraints().end();conit++){
				//if the taxon being added isn't in the backbone, it can go anywhere
				if(((*conit).IsBackbone() == false) || (*conit).GetBackboneMask().ContainsTaxon(nd->nodeNum)){
					ReconNode broken(otherDes->nodeNum, 0, 0.0, false);
					compat = SwapAllowedByConstraint((*conit), nd, &broken, proposed, mask);
					if(compat == false) break;
					}
				}
			}while(compat == false);

		// replace puts connection in the tree where otherDes had been
		otherDes->SubstituteNodeWithRespectToAnc(connector);

		//add otherDes back to the tree as the sister to the new tip
		connector->AddDes(otherDes);
		numBranchesAdded++;//numBranchesAdded needs to be incremented twice because a total of two branches have been added
		bipartCond = DIRTY;
		}
	numBranchesAdded++;
	numNodesAdded++;
	numTipsAdded++;
	}

void Tree::MimicTopologyButNotInternNodeNums(const TreeNode *copySource,TreeNode *replicate,int &placeInAllNodes){
	//used in recombine so internal node nodeNums don't have to match
	TreeNode *tempno=copySource->left;
	assert(copySource->left);
	while(tempno)
		{if(tempno->left)
			{//tempno isn't a terminal
			placeInAllNodes=FindUnusedNode(placeInAllNodes);
			allNodes[placeInAllNodes]->dlen=tempno->dlen;
//			allNodes[placeInAllNodes]->CopyOneClaIndex(copySource, claMan);
			MimicTopologyButNotInternNodeNums(tempno,replicate->AddDes(allNodes[placeInAllNodes]),placeInAllNodes);
			}
		else
			{allNodes[tempno->nodeNum]->dlen=tempno->dlen;
			replicate->AddDes(allNodes[tempno->nodeNum]);
			}
		tempno=tempno->next;
		}
	}

void Tree::RecombineWith( Tree *t, bool sameModel, FLOAT_TYPE optPrecision ){
	//note that this function will loop infinately right now if the tree is too small
	//(ie, there are no suitable nodes to choose to recombine with)

	//mark all of the tags as present in this;
	for(int i=1;i<=numTipsTotal;i++)
		taxtags[i]=0;

	// Pick a random internal node that is the source of the subtree that will be copied into both trees
	int k;
	TreeNode* cop;
	bool sfound=false;

	while(!sfound){//find a non trivial clade to add to this
		//k = rnd.random_int( t->numBranchesAdded-1);
		//cop = t->root->FindNode( ++k);
		//don't bother picking terminal nodes
		k=t->GetRandomInternalNode();
		cop=t->allNodes[k];
		if(cop->left->left || cop->right->left){ // cop isn't a two node sub tree
			if(cop->anc) //check to make sure there are at least 2 nodes "below"cop on the source tree
					{if(cop->anc->anc)
						sfound=true;
					else
						{if(t->root->left!=cop)
							{if(t->root->left->left)
								sfound=true;
							}
						if(!sfound && t->root->left->next!=cop)
							{if(t->root->left->next->left)
								sfound=true;
							}
						if(!sfound && t->root->right!=cop)
							{if(t->root->right->left)
								sfound=true;
							}
						}
					}
				}
			}
	//Prune terminals off of this to prepare for attachement of a copy of cop
	cop->left->MarkTerminals(taxtags);
	for(int i=1;i<=numTipsTotal;i++){
		if(taxtags[i]){
			//before removing the tip, trace dirtyness from its anc to the root
			//make sure to set any
			TraceDirtynessToRoot(allNodes[i]->anc);
//			TraceDirtynessToRoot(allNodes[i]);
			allNodes[i]->Prune();
			if(root->left->next==root->right) MakeTrifurcatingRoot(false, true);
			}
		}
	int numAttachedToRoot=root->CountBranches(0);

	//what we'd like to do now is make the nodeNums of the subtree that will be attached to this
	//the same as they were in the source tree.  This will require swapping some nodes in the allNodes array,
	//but will simplify other things, and allow us not to recalc some clas.  This is a bit dangerous though, as
	//the nodeNums in this that correspond to those in the cop subtree are now technically free, but are still
	//marked as attached.  There should still be one node in this marked as unattached that will be used for
	//the connector
	SwapAndFreeNodes(cop);

	// Pick a random node whose branch we will bisected by the new subtree.
	int n = rnd.random_int( numAttachedToRoot );
	TreeNode* broken = root->FindNode( ++n );
	assert(broken->anc);//broken can't be the root;

	//DZ 7-6 rewritting this so that broken keeps it's original dlen and connector has a new one
	//generated.  Exactly how this would be best done is not clear.  For now picking uniform[0.05,0.2]
	TreeNode *connector;
	int nextUnconnectedNode=FindUnusedNode(numTipsTotal+1);
	connector=allNodes[nextUnconnectedNode];
	connector->left=connector->right=NULL;
	broken->SubstituteNodeWithRespectToAnc(connector);
	connector->AddDes(broken);
	connector->AddDes(allNodes[cop->nodeNum]);
	MimicTopo(cop, 1, sameModel);

	//place connector midway along the broken branch
	connector->dlen=broken->dlen*ZERO_POINT_FIVE;
	broken->dlen-=connector->dlen;

	TraceDirtynessToRoot(connector);
	OptimizeBranchesAroundNode(connector, optPrecision, 0);
	}

const TreeNode *Tree::ContainsBipartition(const Bipartition &bip) const {
	//note that this doesn't work for terminals (but there's no reason to call for them anyway)
	//find a taxon that appears "on" in the bipartition

	//turning this back on
	int tax=bip.FirstPresentTaxon();

	//now start moving down the tree from taxon 1 until a bipart that
	//conflicts or a match is found
	//TreeNode *nd=allNodes[1]->anc;
	TreeNode *nd=allNodes[tax]->anc;
	while(nd->anc){
		if(nd->bipart.IsASubsetOf(bip) == false) return NULL;
		else if(nd->bipart.EqualsEquals(bip)) return nd;
		else nd=nd->anc;
		}
	return NULL;
	}

const TreeNode *Tree::ContainsBipartitionOrComplement(const Bipartition &bip)  const {
	//this version will detect if the same bipartition exists in the trees, even
	//if it is in different orientation, which could happen due to rooting
	//differences

	//NOTE: This requires that the bipartitions are "standardized" meaning that
	//the one bit is always "on".  In general in other places we do not need that
	//to be the case
	if(bipartCond != CLEAN_STANDARDIZED){
		if(bipartCond == CLEAN_UNSTANDARDIZED)
			root->StandardizeBipartition();
		else
			CalcBipartitions(true);
		}

	//find a taxon that appears "on" in the bipartition
	int tax=bip.FirstPresentTaxon();

	//now start moving down the tree from that taxon until a bipart that
	//conflicts or a match is found
	//7/17/07 changing this to start from the trivial terminal branch, rather
	//then its anc
	TreeNode *nd=allNodes[tax];
	while(nd->anc){
		if(nd->bipart.IsASubsetOf(bip) == false) break;
		else if(nd->bipart.EqualsEquals(bip)) return nd;
		else nd=nd->anc;
		}

	//find a taxon that is NOT "on" in the bipartition
	tax=bip.FirstNonPresentTaxon();

	//now start moving down the tree from that taxon until a bipart that
	//conflicts or a match is found
	//7/17/07 changing this to start from the trivial terminal branch, rather
	//then its anc
	nd=allNodes[tax];
	while(nd->anc){
		//if(nd->bipart.ComplementIsASubsetOf(bip) == false){
		if(bip.IsASubsetOf(nd->bipart) == false){
			return NULL;
			}
		else if(nd->bipart.EqualsEquals(bip)) return nd;
		else nd=nd->anc;
		}

	return NULL;
	}

const TreeNode *Tree::ContainsMaskedBipartitionOrComplement(const Bipartition &bip, const Bipartition &mask) const {
	//as in ContainsMaskedBipartitionOrComplement, but bits not on in the
	//mask are ignored

	//NOTE: This requires that the bipartitions are "standardized" meaning that
	//the one bit is always "on".  In general in other places we do not want that
	//to be the case
	if(bipartCond != CLEAN_STANDARDIZED){
		if(bipartCond == CLEAN_UNSTANDARDIZED)
			root->StandardizeBipartition();
		else
			CalcBipartitions(true);
		}

	//find a taxon that appears "on" in the bipartition and is on in the mask
	Bipartition temp = bip;
	temp.AndEquals(mask);
	int tax=temp.FirstPresentTaxon();

	//now start moving down the tree from that taxon until we find a
	//match or reach the root
	TreeNode *nd=allNodes[tax]->anc;
	temp = bip;
	temp.Complement();
	while(nd->anc){
		if(nd->bipart.MaskedEqualsEquals(bip, mask)) return nd;
		if(nd->bipart.MaskedEqualsEquals(temp, mask)) return nd;
		else nd=nd->anc;
		}

	//find a taxon that is NOT "on" in the bipartition
	temp = bip;
	temp.Complement();
	temp.AndEquals(mask);
	tax=temp.FirstPresentTaxon();

	//now start moving down the tree from that taxon until we find a
	//match or reach the root
	nd=allNodes[tax]->anc;
	temp = bip;
	temp.Complement();
	while(nd->anc){
		if(nd->bipart.MaskedEqualsEquals(bip, mask)) return nd;
		if(nd->bipart.MaskedEqualsEquals(temp, mask)) return nd;
		else nd=nd->anc;
		}

	return NULL;
	}

int Tree::SubtreeBasedRecombination( Tree *t, int recomNodeNum, bool sameModel, FLOAT_TYPE optPrecision){
	//this will work more or less like the normal bipartition based recombination, except
	//that the node at which the recombination will occur will be passed in from the population
	//which knows what subtree each remote is working on

	//we are assuming that the recomNodeNum represents the same bipartition (subtree) in each tree

	TreeNode *tonode=allNodes[recomNodeNum];
	TreeNode *fromnode=t->allNodes[recomNodeNum];

	tonode->MarkUnattached(true);
	SwapAndFreeNodes(fromnode);
	//manually set up the base of the subtree in the totree and point tonode to it
	TreeNode *tempanc=tonode->anc;
	TreeNode *tempnext=tonode->next;
	TreeNode *tempprev=tonode->prev;
	if(tempanc->left==tonode){
		tempanc->left=allNodes[fromnode->nodeNum];
		tonode=tempanc->left;
		}
	else if(tempanc->right==tonode){
		tempanc->right=allNodes[fromnode->nodeNum];
		tonode=tempanc->right;
		}
	else{
		tempanc->left->next=allNodes[fromnode->nodeNum];
		tonode=tempanc->left->next;
		}
	tonode->anc=tempanc;
	tonode->next=tempnext;
	tonode->prev=tempprev;
	if(tempnext) tempnext->prev=tonode;
	if(tempprev) tempprev->next=tonode;
	MimicTopo(fromnode, 1, sameModel);
	if(sameModel==true)
		CopyClaIndecesInSubtree(fromnode, true);
	else DirtyNodesInSubtree(tonode);

	SweepDirtynessOverTree(tonode);

	//try branch length optimization of tonode's branch, to make sure it fits in it's new tree background
	OptimizeBranchLength(optPrecision, tonode, true);
	return 1;
	}


bool Tree::IdenticalSubtreeTopology(const TreeNode *other){
	//This should not be called with the root, and only detects identical subtrees
	//in the same orientation (ie rooting can fool it)
	assert(other->IsNotRoot());
	bool identical;

	if(other->IsRoot() == false){
		if(other->IsTerminal()) return true;
		identical=(ContainsBipartition(other->bipart) != NULL);
		if(identical==true){
			identical=IdenticalSubtreeTopology(other->left);
			if(identical==true)
				identical=IdenticalSubtreeTopology(other->right);
			}
		}

	return identical;
	}

bool Tree::IdenticalTopology(const TreeNode *other){
	//this is intitially called with the root, it will detect any difference in the
	//overall topology, but assumes the same rooting
	bool identical;
	//NOTE: This requires that the bipartitions are "standardized" meaning that
	//the one bit is always "on".  In general in other places we do not need that
	//to be the case
	if(bipartCond != CLEAN_STANDARDIZED){
		if(bipartCond == CLEAN_UNSTANDARDIZED)
			root->StandardizeBipartition();
		else
			CalcBipartitions(true);
		}

	if(other->IsRoot() == false){
		if(other->IsTerminal()) return true;
		identical= (ContainsBipartition(other->bipart) != NULL);
		if(identical==true){
			identical=IdenticalTopology(other->left);
			if(identical==true)
				identical=IdenticalTopology(other->right);
			}
		}
	else{
		TreeNode *nd=other->left;
		while(nd != NULL){
			identical=IdenticalTopology(nd);
			if(identical == false){
				return identical;
				}
			nd=nd->next;
			}
		}
	return identical;
	}

bool Tree::IdenticalTopologyAllowingRerooting(const TreeNode *other){
	//this is intitially called with the root, it will detect any difference in the
	//overall topology
	bool identical = true;
	//NOTE: This requires that the bipartitions are "standardized" meaning that
	//the one bit is always "on".  In general in other places we do not need that
	//to be the case
	if(bipartCond != CLEAN_STANDARDIZED){
		if(bipartCond == CLEAN_UNSTANDARDIZED)
			root->StandardizeBipartition();
		else
			CalcBipartitions(true);
		}

	if(other->IsTerminal()) return true;
	if(other->IsRoot() == false)
		identical = (ContainsBipartitionOrComplement(other->bipart) != NULL);
	TreeNode *nd=other->left;
	while(identical && nd != NULL){
		identical = IdenticalTopologyAllowingRerooting(nd);
		if(identical == false) break;
		nd=nd->next;
		}
	return identical;
/*
	if(other->IsRoot() == false){
		if(other->IsTerminal()) return true;
		identical= (ContainsBipartitionOrComplement(*other->bipart) != NULL);
		if(identical==true){
			identical=IdenticalTopologyAllowingRerooting(other->left);
			if(identical==true)
				identical=IdenticalTopologyAllowingRerooting(other->right);
			}
		}
	else{
		TreeNode *nd=other->left;
		while(nd != NULL){
			identical=IdenticalTopologyAllowingRerooting(nd);
			if(identical == false){
				return identical;
				}
			nd=nd->next;
			}
		}
	return identical;
*/	}

int Tree::BipartitionBasedRecombination( Tree *t, bool sameModel, FLOAT_TYPE optPrecision){
	//find a bipartition that is shared between the trees
	const TreeNode *fromnode;
	TreeNode *tonode;
	bool found=false;
	int tries=0;
	CalcBipartitions(true);
	t->CalcBipartitions(true);
	while(!found && (++tries<50)){
		int i;
		do{
			i=GetRandomInternalNode();
			//WTF!!!  How did this work?
			}while((allNodes[i]->left->IsTerminal() && allNodes[i]->right->IsTerminal()));
			//}while((t->allNodes[i]->left->IsTerminal() && t->allNodes[i]->right->IsTerminal()));
		fromnode=t->ContainsBipartitionOrComplement(allNodes[i]->bipart);
		if(fromnode != NULL){
			//OK the biparts match, but see if they share the same clas!!!!
			//Not much point in scoring them then.
			tonode=allNodes[i];
			if(!((tonode->nodeNum == fromnode->nodeNum) && (tonode->claIndexDown == fromnode->claIndexDown))){
				if(IdenticalSubtreeTopology(fromnode->left)==false) found=true;
				if(found==false) if(IdenticalSubtreeTopology(fromnode->right)==false) found=true;
				}
			}
		}
		//sum the two subtrees as if they were the root to see which is better in score
/*		if(found==true){
			FLOAT_TYPE toscore, fromscore;
			toscore=SubTreeScore(tonode);
			fromscore=t->SubTreeScore(fromnode);

			if(fromscore > (toscore + .1)){
				found=true;
				break;
				}
			else found=false;
			}
*/
	if(found==true){
		tonode->MarkUnattached(true);
		SwapAndFreeNodes(fromnode);
		//manually set up the base of the subtree in the totree and point tonode to it
		TreeNode *tempanc=tonode->anc;
		TreeNode *tempnext=tonode->next;
		TreeNode *tempprev=tonode->prev;
		if(tempanc->left==tonode){
			tempanc->left=allNodes[fromnode->nodeNum];
			tonode=tempanc->left;
			}
		else if(tempanc->right==tonode){
			tempanc->right=allNodes[fromnode->nodeNum];
			tonode=tempanc->right;
			}
		else{

			tempanc->left->next=allNodes[fromnode->nodeNum];
			tonode=tempanc->left->next;
			}
		tonode->anc=tempanc;
		tonode->next=tempnext;
		tonode->prev=tempprev;
		if(tempnext) tempnext->prev=tonode;
		if(tempprev) tempprev->next=tonode;
		MimicTopo(fromnode, 1, sameModel);
		if(sameModel==true) 
			CopyClaIndecesInSubtree(fromnode, true);
		else DirtyNodesInSubtree(tonode);

		//try branch length optimization of tonode's branch, to make sure it fits in it's new tree background
		SweepDirtynessOverTree(tonode);
		//OptimizeBranchLength(optPrecision, tonode, true);
		OptimizeBranchesWithinRadius(tonode, optPrecision, 0, NULL);

		Score(tonode->nodeNum);
		bipartCond = DIRTY;
		}
	else return -1;
	return 1;
	}

//this is essentially a version of TopologyMutator that goes through cut nodes in order
//and for each cut node goes through the broken nodes in order.  The swaps are performed
//on a temporary tree
void Tree::DeterministicSwapperByCut(Individual *source, double optPrecision, int range, bool furthestFirst){

	int swapNum=0;

	Individual tempIndiv;
	tempIndiv.treeStruct=new Tree();

	tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, source);

	//ensure that the starting tree is optimal up to the required precision
	FLOAT_TYPE imp = 999.9;
	do{
		imp = tempIndiv.treeStruct->OptimizeAllBranches(optPrecision);
		}while(imp > 0.0);

	outman.UserMessage("starting score:%f", tempIndiv.treeStruct->lnL);

	char str[50];
	if(furthestFirst) sprintf(str, "determImpsCutR.%d.%f.tre", range, optPrecision);
	else sprintf(str, "determImpsCut.%d.%f.tre", range, optPrecision);
	ofstream better(str);
	better.precision(9);
	data->BeginNexusTreesBlock(better);

#ifdef OUTPUT_ALL
	if(furthestFirst) sprintf(str, "determAllCutR.%d.%f.tre", range, optPrecision);
	else sprintf(str, "determAllCut.%d.%f.tre", range, optPrecision);

	ofstream all(str);
	data->BeginNexusTreesBlock(all);
#endif

	if(furthestFirst) sprintf(str, "determCutR%d.%f.log", range, optPrecision);
	else sprintf(str, "determCut%d.%f.log", range, optPrecision);
	FILE *log = fopen(str, "w");

	//allocate a treeString
	double taxsize=log10((double) ((double)data->NTax())*data->NTax()*2);
	int stringSize=(int)((data->NTax()*2)*(10+DEF_PRECISION));
	char *treeString=new char[stringSize];
	stringSize--;
	treeString[stringSize]='\0';
	bool newBest=false;
	attemptedSwaps.ClearAttemptedSwaps();

	int startC, c=1;

	int acceptedSwaps = 0;
	startC = c;

	while(1){
		TreeNode * cut = tempIndiv.treeStruct->allNodes[c];
		tempIndiv.treeStruct->GatherValidReconnectionNodes(range, cut, NULL);
		tempIndiv.treeStruct->sprRang.SortByDist();
		if(furthestFirst) tempIndiv.treeStruct->sprRang.Reverse();

		for(list<ReconNode>::iterator b = tempIndiv.treeStruct->sprRang.begin();b != tempIndiv.treeStruct->sprRang.end();b++){
			ReconNode *broken = &(*b);

			//log the swap about to be performed.  Although this func goes through the swaps in order,
			//there will be duplication because of the way that NNIs are performed.  Two different cut
			//nodes can be reconnected with an NNI such that the same topology results
			bool unique=false;
			Bipartition proposed;
			CalcBipartitions(true);
			proposed.FillWithXORComplement(cut->bipart, tempIndiv.treeStruct->allNodes[broken->nodeNum]->bipart);
			unique = attemptedSwaps.AddSwap(proposed, cut->nodeNum, broken->nodeNum, broken->reconDist);

			if(unique){
				swapNum++;
				if(swapNum %100 == 0) fprintf(log, "%d\t%d\t%f\n", swapNum, acceptedSwaps, lnL);
				if(broken->withinCutSubtree == true){
					tempIndiv.treeStruct->ReorientSubtreeSPRMutate(cut->nodeNum, broken, optPrecision);
					}
				else{
					tempIndiv.treeStruct->SPRMutate(cut->nodeNum, broken, optPrecision, 0);
					}

#ifdef OUTPUT_ALL
				tempIndiv.treeStruct->root->MakeNewick(treeString, false, true);
				all << "tree " << c << "." << b->reconDist << "= [&U][" << lnL << "]" << treeString << ";" << endl;
#endif

				if(tempIndiv.treeStruct->lnL > (lnL+optPrecision)){

					outman.UserMessage("%f\t%f\t%d\t%d", tempIndiv.treeStruct->lnL, lnL - tempIndiv.treeStruct->lnL, c, b->reconDist);
					source->CopySecByRearrangingNodesOfFirst(source->treeStruct, &tempIndiv, true);
					lnL = tempIndiv.treeStruct->lnL;

					tempIndiv.treeStruct->root->MakeNewick(treeString, false, true);
					better << "tree " << c << "." << b->reconDist << "= [&U][" << lnL << "]" << treeString << ";" << endl;
					newBest = true;
					acceptedSwaps++;
					attemptedSwaps.ClearAttemptedSwaps();
					break;
					}
				else{
					tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, source, true);
					}
				}
			}
		c++;
		if(c == allNodes.size()) c = 1;
		if(newBest == true){
			startC = c;
			newBest = false;
			}
		else if(c == startC){
			outman.UserMessage("done. %d swaps, %d accepted", swapNum, acceptedSwaps);
			break;
			}
		}

	better << "end;";
	better.close();
	delete []treeString;
	fclose(log);

	tempIndiv.treeStruct->RemoveTreeFromAllClas();
	delete tempIndiv.treeStruct;
	tempIndiv.treeStruct=NULL;
	}


// returns true if we have a new best tree
bool Tree::DeterministicSwapByDistAroundNode(
  Individual & orig,
  Individual & tempIndiv, 
  TreeNode * cut,
  double optPrecision,
  int range,
  int currentDist,
  int c,
  ostream & better,
  int * swapNum,
  int * acceptedSwaps,
  FILE * log )  {
	const size_t stringSize=(size_t)((data->NTax()*2)*(10+DEF_PRECISION));
	std::string treeStringCleanup(stringSize, '\0');
	char * treeString = const_cast<char *>(treeStringCleanup.c_str());

	GatherValidReconnectionNodes(range, cut, NULL);
	sprRang.SortByDist();
	for(list<ReconNode>::iterator b = sprRang.GetFirstNodeAtDist(currentDist); b != sprRang.end() && b->reconDist == currentDist; b++) {
		ReconNode * broken = &(*b);

		//log the swap about to be performed.  Although this func goes through the swaps in order,
		//there will be duplication because of the way that NNIs are performed.  Two different cut
		//nodes can be reconnected with an NNI such that the same topology results
		bool unique=false;
		Bipartition proposed;
		CalcBipartitions(true);
		proposed.FillWithXORComplement(cut->bipart, allNodes[broken->nodeNum]->bipart);
		unique = attemptedSwaps.AddSwap(proposed, cut->nodeNum, broken->nodeNum, broken->reconDist);

		if(unique){
			if (swapNum)
				(*swapNum) += 1;
			if(broken->withinCutSubtree == true) {
				tempIndiv.treeStruct->ReorientSubtreeSPRMutate(cut->nodeNum, broken, optPrecision);
				}
			else {
				tempIndiv.treeStruct->SPRMutate(cut->nodeNum, broken, optPrecision, 0);
				}

#			ifdef OUTPUT_ALL
				tempIndiv.treeStruct->root->MakeNewick(treeString, false, true);
				all << "tree " << c << "." << b->reconDist << "= [&U][" << lnL << "]" << treeString << ";" << endl;
#			endif

			if(tempIndiv.treeStruct->lnL > (lnL+optPrecision)){
				outman.UserMessage("%f\t%f\t%d\t%d", tempIndiv.treeStruct->lnL, lnL - tempIndiv.treeStruct->lnL, c, b->reconDist);

				orig.CopySecByRearrangingNodesOfFirst(orig.treeStruct, &tempIndiv, true);
				lnL = tempIndiv.treeStruct->lnL;

				tempIndiv.treeStruct->root->MakeNewick(treeString, false, true);
				better << "tree " << c << "." << b->reconDist << "= [&U][" << lnL << "]" << treeString << ";" << endl;
				if (acceptedSwaps)
					(*acceptedSwaps) += 1;
				attemptedSwaps.ClearAttemptedSwaps();
				return true;
				}
			else{
				tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &orig, true);
				}
			if(acceptedSwaps && log && swapNum &&  ((*swapNum) % 100 == 0))
				fprintf(log, "%d\t%d\t%f\n", *swapNum, *acceptedSwaps, lnL);
			}
		}
	return false;
}
//this is essentially a version of TopologyMutator that goes through cut nodes in order
//and for each cut node goes through the broken nodes in order.  It the swaps are performed
//on a temporary tree
void Tree::DeterministicSwapperByDist(Individual *source, double optPrecision, int range, bool furthestFirst){

	Individual tempIndiv;
	tempIndiv.treeStruct=new Tree();
	tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, source);

	//ensure that the starting tree is optimal up to the required precision
	FLOAT_TYPE imp = 999.9;
	do {
		imp = tempIndiv.treeStruct->OptimizeAllBranches(optPrecision);
	}
	while(imp > 0.0);

	outman.UserMessage("starting score:%f", tempIndiv.treeStruct->lnL);

	char str[50];
	if(furthestFirst) sprintf(str, "determImpsDistR.%d.%f.tre", range, optPrecision);
	else sprintf(str, "determImpsDist.%d.%f.tre", range, optPrecision);

	ofstream better(str);
	better.precision(9);
	data->BeginNexusTreesBlock(better);

#	ifdef OUTPUT_ALL
		if(furthestFirst)
			sprintf(str, "determAllDistR.%d.%f.tre", range, optPrecision);
		else
			sprintf(str, "determAllDist.%d.%f.tre", range, optPrecision);
	
		ofstream all(str);
		data->BeginNexusTreesBlock(all);
#	endif

	if(furthestFirst)
		sprintf(str, "determDistR%d.%f.log", range, optPrecision);
	else
		sprintf(str, "determDist%d.%f.log", range, optPrecision);
	FILE *log = fopen(str, "w");

	//allocate a treeString
	double taxsize=log10((double) ((double)data->NTax())*data->NTax()*2);
	attemptedSwaps.ClearAttemptedSwaps();
	int c = 1;
	int currentDist = (furthestFirst ?  range : 1);
	const int distIncr = (furthestFirst ? -1 : 1);
	int acceptedSwaps = 0;
	int startC = c;
	int swapNum=0;
	do {
		const bool newBest = this->DeterministicSwapByDistAroundNode(*source, tempIndiv, allNodes[c], optPrecision, range, currentDist, c, better, &swapNum, &acceptedSwaps, log);
		c++;
		if(c == allNodes.size())
			c = 1;
		if (newBest) {
			startC = c;
			currentDist = (furthestFirst ? range : 1);
		}
		else if(c == startC){
			currentDist += distIncr;
			outman.UserMessage("dist = %d", currentDist);
		}
	}
	while(currentDist <= range && currentDist > 0);

	outman.UserMessage("done. %d swaps, %d accepted", swapNum, acceptedSwaps);

	better << "end;";
	better.close();
	fclose(log);

	tempIndiv.treeStruct->RemoveTreeFromAllClas();
	delete tempIndiv.treeStruct;
	tempIndiv.treeStruct=NULL;
	}

void Tree::FillAllSwapsList(ReconList *cuts, int reconLim){
	CalcBipartitions(true);
	for(int i=1;i<allNodes.size();i++) cuts[i].clear();
	for(int i=1;i<allNodes.size();i++){
		GatherValidReconnectionNodes(cuts[i], reconLim, allNodes[i], NULL);
		}
	}

unsigned Tree::FillWeightsForAllSwaps(ReconList *cuts, double *cutWeights){
	double tot = 0.0, runningTot = 0.0;
	for(int i=1;i<allNodes.size();i++)
		tot += cuts[i].size();
	for(int i=1;i<allNodes.size();i++){
		runningTot += (double) cuts[i].size() / tot;
		cutWeights[i] = runningTot;
		}
	cutWeights[allNodes.size()] = 1.0;
	return (unsigned) tot;
	}

//this is essentially a version of TopologyMutator that goes through cut nodes in order
//and for each cut node goes through the broken nodes in some order. The swaps are performed
//on a temporary tree
void Tree::DeterministicSwapperRandom(Individual *source, double optPrecision, int range){

	TreeNode *cut;
	int swapNum=0;

	Individual tempIndiv;
	tempIndiv.treeStruct=new Tree();

	tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, source);

	//ensure that the starting tree is optimal up to the required precision
	FLOAT_TYPE imp = 999.9;
	do{
		imp = tempIndiv.treeStruct->OptimizeAllBranches(optPrecision);
		}while(imp > 0.0);

	outman.UserMessage("starting score:%f", tempIndiv.treeStruct->lnL);

	char str[50];
	sprintf(str, "determImpsRand.%d.%f.tre", range, optPrecision);
	ofstream better(str);
	better.precision(9);
	data->BeginNexusTreesBlock(better);

#ifdef OUTPUT_ALL
	sprintf(str, "determAllRand.%d.%f.tre", range, optPrecision);

	ofstream all(str);
	data->BeginNexusTreesBlock(all);
#endif

	sprintf(str, "determRand%d.%f.log", range, optPrecision);
	FILE *log = fopen(str, "w");

	//allocate a treeString
	double taxsize=log10((double) ((double)data->NTax())*data->NTax()*2);
	int stringSize=(int)((data->NTax()*2)*(10+DEF_PRECISION));
	char *treeString=new char[stringSize];
	stringSize--;
	treeString[stringSize]='\0';
	bool newBest=false;
	attemptedSwaps.ClearAttemptedSwaps();
	int c=1;

	//zeroth element won't be used, for clarity of indexing
	vector<ReconList> cuts(allNodes.size()+1);
	vector<double> cutWeights(allNodes.size()+1);
	tempIndiv.treeStruct->FillAllSwapsList(&cuts[0], range);
	unsigned swapsLeft = tempIndiv.treeStruct->FillWeightsForAllSwaps(&cuts[0], &cutWeights[0]);

	int acceptedSwaps = 0;
	int swapsOnCurrent=0;
	do{
		double r = rnd.uniform();
		c = 1;
		while(cutWeights[c] < r) c++;
		cut = tempIndiv.treeStruct->allNodes[c];
		listIt b = cuts[c].NthElement(rnd.random_int(cuts[c].size()));
		ReconNode *broken = &(*b);

		//log the swap about to be performed.  Although this func goes through the swaps in order,
		//there will be duplication because of the way that NNIs are performed.  Two different cut
		//nodes can be reconnected with an NNI such that the same topology results
		bool unique=false;
		newBest = false;
		Bipartition proposed;
		CalcBipartitions(true);
		proposed.FillWithXORComplement(cut->bipart, tempIndiv.treeStruct->allNodes[broken->nodeNum]->bipart);
		unique = attemptedSwaps.AddSwap(proposed, cut->nodeNum, broken->nodeNum, broken->reconDist);

		if(unique){
			swapNum++;
			swapsOnCurrent++;
			if(broken->withinCutSubtree == true){
				tempIndiv.treeStruct->ReorientSubtreeSPRMutate(cut->nodeNum, broken, optPrecision);
				}
			else{
				tempIndiv.treeStruct->SPRMutate(cut->nodeNum, broken, optPrecision, 0);
				}
#ifdef OUTPUT_ALL
			tempIndiv.treeStruct->root->MakeNewick(treeString, false, true);
			all << "tree " << c << "." << b->nodeNum << "." << b->reconDist << "." << swapsOnCurrent << " = [&U][" << lnL << "]" << treeString << ";" << endl;
#endif

			if(tempIndiv.treeStruct->lnL > (lnL+optPrecision)){
				outman.UserMessage("%f\t%f\t%d\t%d", tempIndiv.treeStruct->lnL, lnL - tempIndiv.treeStruct->lnL, c, b->reconDist);
				source->CopySecByRearrangingNodesOfFirst(source->treeStruct, &tempIndiv, true);
				lnL = tempIndiv.treeStruct->lnL;

				tempIndiv.treeStruct->root->MakeNewick(treeString, false, true);
				better << "tree " << c << "." << b->nodeNum << "." << b->reconDist << "= [&U][" << lnL << "]" << treeString << ";" << endl;
				newBest = true;
				acceptedSwaps++;
				outman.UserMessage("%d swaps before reset", swapsOnCurrent);
				swapsOnCurrent = 0;
				attemptedSwaps.ClearAttemptedSwaps();
				tempIndiv.treeStruct->FillAllSwapsList(&cuts[0], range);
				}
			else{
				tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, source, true);
				}
			}
		else{
			if(broken->reconDist != 1) throw ErrorException("nonunique swap > NNI found! %d %d %d", c, b->nodeNum, b->reconDist);
			}
		if(newBest == false)//if the swap either wasn't better or wasn't unique
			cuts[c].RemoveElement(b);
		if(swapNum %100 == 0) fprintf(log, "%d\t%d\t%f\n", swapNum, acceptedSwaps, lnL);
		swapsLeft = tempIndiv.treeStruct->FillWeightsForAllSwaps(&cuts[0], &cutWeights[0]);
		}while(swapsLeft);

	outman.UserMessage("%d swaps before completion", swapsOnCurrent);

}

//this function now returns the reconnection distance, with it being negative if its a
//subtree reorientation swap
int Tree::TopologyMutator(FLOAT_TYPE optPrecision, int range, int subtreeNode){
	//All topology mutations go through here now.  Range will be 1 in the case of NNI's
	//Range will be some small number in the case of limSPR's and will be 999999 in the case
	//of random SPR's
	TreeNode *cut;
	ReconNode *broken;
	bool unique;

#ifdef EQUIV_CALCS
	dirtyEQ = true;
#endif

	int err=0;
	int ret=0;
	int tryNum = 0;
	do{
		do{
			cut=allNodes[GetRandomNonRootNode()];
			GatherValidReconnectionNodes(range, cut, NULL);
			}while(sprRang.size()==0);

		if((FloatingPointEquals(uniqueSwapBias, 1.0, max(1.0e-8, GARLI_FP_EPS * 2.0)) && FloatingPointEquals(distanceSwapBias, 1.0, max(1.0e-8, GARLI_FP_EPS * 2))) || range < 0)
			broken = sprRang.RandomReconNode();
		else{//only doing this on limSPR and NNI
			err = AssignWeightsToSwaps(cut);
			err = err && (tryNum++ < 5);
#ifdef SWAP_BASED_TERMINATION
			if(!err){
#else
			if(1){
				//this was a stupid bug.  Err was being paid attention by looping over the
				//outer do loop because it was not being reset below when returning from ReorientSubtreeSPR
				//as it is with normal SPR
				err = 0;
#endif
				sprRang.CalcProbsFromWeights();
				broken = sprRang.ChooseNodeByWeight();
				}
			}

#ifdef SWAP_BASED_TERMINATION
		if(!err){
#else
		if(1){
#endif
			//log the swap about to be performed
			if( ! ((uniqueSwapBias == 1.0 && distanceSwapBias == 1.0) || range < 0)){
				Bipartition proposed;
				CalcBipartitions(true);
				proposed.FillWithXORComplement(cut->bipart, allNodes[broken->nodeNum]->bipart);
				unique = attemptedSwaps.AddSwap(proposed, cut->nodeNum, broken->nodeNum, broken->reconDist);
				uniqueSwapTried = uniqueSwapTried || unique;
				//uniqueSwapTried = uniqueSwapTried || attemptedSwaps.AddSwap(proposed, cut->nodeNum, broken->nodeNum, broken->reconDist);
				}
			//else if(! ((uniqueSwapBias == 1.0 && distanceSwapBias == 1.0) && range < 0)){
			else{
				//this means that we are doing an unlimited SPR, which we don't keep track of
				unique = false;
				}

			if(broken->withinCutSubtree == true){
				#ifdef OPT_DEBUG
					optsum << "reorientSPR\t" << broken->reconDist << "\t" << range << "\n";
				#endif
				#ifdef VARIABLE_OPTIMIZATION
					if(unique == true) ReorientSubtreeSPRMutateDummy(cut->nodeNum, broken, optPrecision);
					else return broken->reconDist * -1;
				#endif
				ReorientSubtreeSPRMutate(cut->nodeNum, broken, optPrecision);
				ret=broken->reconDist * -1;
				}
			else{
				#ifdef OPT_DEBUG
					optsum << "SPR\t" << broken->reconDist << "\t" << range << "\n";
				#endif
				#ifdef VARIABLE_OPTIMIZATION
					if(unique == true) err=SPRMutateDummy(cut->nodeNum, broken, optPrecision, subtreeNode);
					else return broken->reconDist;
				#endif
				err=SPRMutate(cut->nodeNum, broken, optPrecision, subtreeNode);
				ret=broken->reconDist;
				}
			#ifdef OUTPUT_UNIQUE_TREES
			if(unique == true){
				output_tree = true;
				//uni.precision(9);
				if(broken->withinCutSubtree == false) uni << "SPR" << "\t" << broken->reconDist << "\t" << lnL << "\t" << cut->nodeNum << "\t" << broken->nodeNum << "\n";
				else uni << "reSPR" << "\t" << broken->reconDist << "\t" << lnL << "\t" << cut->nodeNum << "\t" << broken->nodeNum << "\n";
				}
			#endif
			}
		}while(err);

#ifndef NDEBUG
	vector<Constraint>::const_iterator conit=GetConstraints().begin();
	for(;conit!=GetConstraints().end();conit++){
		const TreeNode *check = NULL;
		if((*conit).IsBackbone())
			check = ContainsMaskedBipartitionOrComplement(conit->GetBipartition(), conit->GetBackboneMask());
		else
			check = ContainsBipartitionOrComplement(conit->GetBipartition());
		if((*conit).IsPositive()) assert(check != NULL);
		else assert(check == NULL);
		}
#endif
	return ret;
	}

void Tree::GatherValidReconnectionNodes(int maxDist, TreeNode *cut, const TreeNode *subtreeNode, Bipartition *partialMask /*=NULL*/){
	/* 7/11/06 making this function more multipurpose
	It now assumes that the cut branch has NOT YET BEEN DETACHED. This is important so that
	when branches are chosen without a viable reconnection due to a constraint another cut
	can be chosen without having the put the tree back together again
	1.	Gather all nodes within maxRange.  This can include nodes that are des of the
		cut node.  In this case the portion of the tree containing the root is considered
		the subtree to be reattached, and the swap would be done by ReorientSubtreeSPRMutate
	2.	Keep information on the potential reconnection nodes, including reconnection distance and
		branchlength distance.  This allows for various schemes of differentially weighting the
		swaps.
	3.	filter out reconnection nodes incompatible with constraints
	*/
	sprRang.clear();
	const TreeNode *center=cut->anc;

	//add the descendent branches
	if(center->left != cut)
		sprRang.AddNode(center->left->nodeNum, 0, (float) center->left->dlen);
	if(center->left->next != cut)
		sprRang.AddNode(center->left->next-> nodeNum, 0, (float) center->left->next->dlen);

	//add either the center node itself or the third descendent in the case of the root
	if(center->IsNotRoot()){
		if(center->anc != subtreeNode)
			sprRang.AddNode(center->nodeNum, 0, (float) center->dlen);
		}
	else{
		if(center->left->next->next != cut)
			sprRang.AddNode(center->left->next->next->nodeNum, 0, (float) center->left->next->next->dlen);
		}

	assert(sprRang.size() == 2);

	for(int curDist = 0; curDist < maxDist || maxDist < 0; curDist++){
		list<ReconNode>::iterator it=sprRang.GetFirstNodeAtDist(curDist);
		if(it == sprRang.end()){
			break; //need this to break out of loop when curDist exceeds any branches in the tree
			}
		for(; it != sprRang.end() && it->reconDist == curDist; it++){
			TreeNode *cur=allNodes[it->nodeNum];
			assert(cur->IsNotRoot());

			if(cur->left!=NULL && cur->left!=cut)
			    sprRang.AddNode(cur->left->nodeNum, curDist+1, (float) (it->pathlength + cur->left->dlen));
			if(cur->right!=NULL && cur->right!=cut)
		    	sprRang.AddNode(cur->right->nodeNum, curDist+1, (float) (it->pathlength + cur->right->dlen));
			if(cur->next!=NULL && cur->next!=cut){
			    sprRang.AddNode(cur->next->nodeNum, curDist+1, (float) (it->pathlength + cur->next->dlen));
			    if(cur->next->next!=NULL && cur->next->next!=cut){//if cur is the left descendent of the root
			    	sprRang.AddNode(cur->next->next->nodeNum, curDist+1, (float) (it->pathlength + cur->next->next->dlen));
			    	}
			    }
			if(cur->prev!=NULL && cur->prev!=cut){
			    sprRang.AddNode(cur->prev->nodeNum, curDist+1, (float) (it->pathlength + cur->prev->dlen));
			    if(cur->prev->prev!=NULL && cur->prev->prev!=cut){//if cur is the right descendent of the root
			    	sprRang.AddNode(cur->prev->prev->nodeNum, curDist+1, (float) (it->pathlength + cur->prev->prev->dlen));
			    	}
			    }
		    if(cur->anc->nodeNum != 0){//if the anc is not the root, add it.
		    	if(cur->anc!=subtreeNode){
			    	sprRang.AddNode(cur->anc->nodeNum, curDist+1, (float) (it->pathlength + cur->anc->dlen));
			 		}
			 	}
		    }
		}

	if(maxDist != 1 && cut->IsInternal()){
		//Gather nodes within the cut subtree to allow SPRs in which the portion of the tree containing
		//the root is considered the subtree to be reattached
		//start by adding cut's left and right
		sprRang.AddNode(cut->left->nodeNum, 0, (float) cut->left->dlen, true);
		sprRang.AddNode(cut->right->nodeNum, 0, (float) cut->right->dlen, true);

		for(int curDist = 0; curDist < maxDist || maxDist < 0; curDist++){
			list<ReconNode>::iterator it=sprRang.GetFirstNodeAtDistWithinCutSubtree(curDist);
			if(it == sprRang.end()){
				break; //need this to break out of loop when curDist exceeds any branches in the tree
				}
			for(; it != sprRang.end() && it->reconDist == curDist; it++){
				TreeNode *cur=allNodes[it->nodeNum];

				if(cur->left!=NULL)
					sprRang.AddNode(cur->left->nodeNum, curDist+1, (float) (it->pathlength + cur->left->dlen), true);
				if(cur->right!=NULL)
		    		sprRang.AddNode(cur->right->nodeNum, curDist+1, (float) (it->pathlength + cur->right->dlen), true);
				if(cur->next!=NULL){
					sprRang.AddNode(cur->next->nodeNum, curDist+1, (float) (it->pathlength + cur->next->dlen), true);
					}
				}
			}
		}

    //remove general unwanted nodes from the subset
	sprRang.RemoveNodesOfDist(0); //remove branches adjacent to cut
//	if(maxDist != 1)
//		sprRang.RemoveNodesOfDist(1); //remove branches equivalent to NNIs

#ifdef CONSTRAINTS
	//now deal with constraints, if any
	if (IsUsingConstraints()) {
		if(sprRang.size() != 0) {
			Bipartition proposed;
			listIt it=sprRang.begin();
			do{
				TreeNode* broken=allNodes[it->nodeNum];
				CalcBipartitions(true);
				proposed.FillWithXORComplement(cut->bipart, allNodes[broken->nodeNum]->bipart);
				bool allowed = true;
				vector<Constraint>::const_iterator conit = Tree::GetConstraints().begin();
				for(; conit != Tree::GetConstraints().end(); conit++){
					allowed = SwapAllowedByConstraint((*conit), cut, &*it, proposed, partialMask);
					if(!allowed) break;
					}
				if(!allowed) it=sprRang.RemoveElement(it);
				else it++;
				}while(it != sprRang.end());
			}
		else return;
		}
#endif
	}

//same as the normal GatherValidReconnectionNodes, but fills ReconList passed in, not the normal tree one
void Tree::GatherValidReconnectionNodes(ReconList &thisList, int maxDist, TreeNode *cut, const TreeNode *subtreeNode, Bipartition *partialMask /*=NULL*/){
	const TreeNode *center=cut->anc;

	//add the descendent branches
	if(center->left != cut)
		thisList.AddNode(center->left->nodeNum, 0, (float) center->left->dlen);
	if(center->left->next != cut)
		thisList.AddNode(center->left->next-> nodeNum, 0, (float) center->left->next->dlen);

	//add either the center node itself or the third descendent in the case of the root
	if(center->IsNotRoot()){
		if(center->anc != subtreeNode)
			thisList.AddNode(center->nodeNum, 0, (float) center->dlen);
		}
	else{
		if(center->left->next->next != cut)
			thisList.AddNode(center->left->next->next->nodeNum, 0, (float) center->left->next->next->dlen);
		}

	assert(thisList.size() == 2);

	for(int curDist = 0; curDist < maxDist || maxDist < 0; curDist++){
		//list<ReconNode>::iterator it=thisList.GetFirstNodeAtDist(curDist);
		listIt it=thisList.GetFirstNodeAtDist(curDist);
		if(it == thisList.end()){
			break; //need this to break out of loop when curDist exceeds any branches in the tree
			}
		for(; it != thisList.end() && it->reconDist == curDist; it++){
			TreeNode *cur=allNodes[it->nodeNum];
			assert(cur->IsNotRoot());

			if(cur->left!=NULL && cur->left!=cut)
			    thisList.AddNode(cur->left->nodeNum, curDist+1, (float) (it->pathlength + cur->left->dlen));
			if(cur->right!=NULL && cur->right!=cut)
		    	thisList.AddNode(cur->right->nodeNum, curDist+1, (float) (it->pathlength + cur->right->dlen));
			if(cur->next!=NULL && cur->next!=cut){
			    thisList.AddNode(cur->next->nodeNum, curDist+1, (float) (it->pathlength + cur->next->dlen));
			    if(cur->next->next!=NULL && cur->next->next!=cut){//if cur is the left descendent of the root
			    	thisList.AddNode(cur->next->next->nodeNum, curDist+1, (float) (it->pathlength + cur->next->next->dlen));
			    	}
			    }
			if(cur->prev!=NULL && cur->prev!=cut){
			    thisList.AddNode(cur->prev->nodeNum, curDist+1, (float) (it->pathlength + cur->prev->dlen));
			    if(cur->prev->prev!=NULL && cur->prev->prev!=cut){//if cur is the right descendent of the root
			    	thisList.AddNode(cur->prev->prev->nodeNum, curDist+1, (float) (it->pathlength + cur->prev->prev->dlen));
			    	}
			    }
		    if(cur->anc->nodeNum != 0){//if the anc is not the root, add it.
		    	if(cur->anc!=subtreeNode){
			    	thisList.AddNode(cur->anc->nodeNum, curDist+1, (float) (it->pathlength + cur->anc->dlen));
			 		}
			 	}
		    }
		}

	if(maxDist != 1 && cut->IsInternal()){
		//Gather nodes within the cut subtree to allow SPRs in which the portion of the tree containing
		//the root is considered the subtree to be reattached
		//start by adding cut's left and right
		thisList.AddNode(cut->left->nodeNum, 0, (float) cut->left->dlen, true);
		thisList.AddNode(cut->right->nodeNum, 0, (float) cut->right->dlen, true);

		for(int curDist = 0; curDist < maxDist || maxDist < 0; curDist++){
			//list<ReconNode>::iterator it=thisList.GetFirstNodeAtDistWithinCutSubtree(curDist);
			listIt it=thisList.GetFirstNodeAtDistWithinCutSubtree(curDist);
			if(it == thisList.end()){
				break; //need this to break out of loop when curDist exceeds any branches in the tree
				}
			for(; it != thisList.end() && it->reconDist == curDist; it++){
				TreeNode *cur=allNodes[it->nodeNum];

				if(cur->left!=NULL)
					thisList.AddNode(cur->left->nodeNum, curDist+1, (float) (it->pathlength + cur->left->dlen), true);
				if(cur->right!=NULL)
		    		thisList.AddNode(cur->right->nodeNum, curDist+1, (float) (it->pathlength + cur->right->dlen), true);
				if(cur->next!=NULL){
					thisList.AddNode(cur->next->nodeNum, curDist+1, (float) (it->pathlength + cur->next->dlen), true);
					}
				}
			}
		}

    //remove general unwanted nodes from the subset
	thisList.RemoveNodesOfDist(0); //remove branches adjacent to cut
	//try removing nni's that would be dupes
	for(listIt it = thisList.begin(); it != thisList.end();){
		if(cut->nodeNum > (*it).nodeNum) it = thisList.RemoveElement(it);
		else it++;
		}

#ifdef CONSTRAINTS
	//now deal with constraints, if any
	if(IsUsingConstraints()){
		Bipartition scratch;
		vector<Constraint>::const_iterator conit=Tree::GetConstraints().begin();
		for(;conit!=Tree::GetConstraints().end();conit++){
			if(thisList.size() != 0){
				listIt it=thisList.begin();
				do{
					//if(AllowedByConstraint(&(*conit), cut, broken, scratch) == false) it=thisList.RemoveElement(it);
					if(SwapAllowedByConstraint((*conit), cut, &*it, scratch, partialMask) == false) it=thisList.RemoveElement(it);
					else it++;
					}while(it != thisList.end());
				}
			else return;
			}
		}
#endif
	}

bool Tree::AssignWeightsToSwaps(TreeNode *cut){
	//Assign weights to each swap (reconnection node) based on
	//some criterion
	CalcBipartitions(true);

	Bipartition proposed;
	list<Swap>::iterator thisSwap;
	bool someUnique = false;

	Swap tmp;

	for(listIt it = sprRang.begin();it != sprRang.end();it++){
		bool found;
		CalcBipartitions(true);
		proposed.FillWithXORComplement(cut->bipart, allNodes[(*it).nodeNum]->bipart);
		tmp.Setup(proposed, cut->nodeNum, (*it).nodeNum, (*it).reconDist);
		thisSwap = attemptedSwaps.FindSwap(tmp, found);

		if(found == false){
			someUnique = true;
			if((*it).reconDist - 1 < 1000)
				(*it).weight = distanceSwapPrecalc[(*it).reconDist - 1];
			else
				(*it).weight = distanceSwapPrecalc[999];
			}
		else{
			if((*thisSwap).Count() < 500)
				(*it).weight = uniqueSwapPrecalc[(*thisSwap).Count()];
			else
				(*it).weight = uniqueSwapPrecalc[499];
			if((*it).reconDist - 1 < 1000)
				(*it).weight *= distanceSwapPrecalc[(*it).reconDist - 1];
			else
				(*it).weight *= distanceSwapPrecalc[999];
/*			if((*it).reconDist - 1 < 1000 && (*thisSwap).Count() < 500)
				(*it).weight = uniqueSwapPrecalc[(*thisSwap).Count()] * distanceSwapPrecalc[(*it).reconDist - 1];
			else (*it).weight = 0.0;
*/			}
		}
	return someUnique==false;
	}

int Tree::SPRMutateDummy(int cutnum, ReconNode *broke, FLOAT_TYPE optPrecision, int subtreeNode){
	//this is just a spoof version of SPRMutate that will perform the same mutation
	//several times with different optimiation settings, but will otherwise
	//maintain exactly the same program flow because it resets the seed

#ifndef VARIABLE_OPTIMIZATION
	assert(0);
#else
	Individual tempIndiv;
	tempIndiv.treeStruct=new Tree();

	Individual sourceIndiv;
	sourceIndiv.treeStruct=this;
	sourceIndiv.mod->CopyModel(this->mod);

	int savedSeed;

	var.precision(10);
	var << "SPR" << "\t" << broke->reconDist << "\t" << lnL << "\t";

	tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &sourceIndiv);
//	FLOAT_TYPE prec[5]={(FLOAT_TYPE).01, (FLOAT_TYPE).5, (FLOAT_TYPE).01, (FLOAT_TYPE).01, (FLOAT_TYPE).01};
	FLOAT_TYPE origThresh = treeRejectionThreshold;
/*
	treeRejectionThreshold = 10000;
	for(int i=0;i<1;i++){
		savedSeed = rnd.seed();
		optCalcs = 0;
		tempIndiv.treeStruct->SPRMutate(cutnum, broke, optPrecision, 0);
		var << tempIndiv.treeStruct->lnL << "\t" << optCalcs << "\t";
		optCalcs = 0;
		rnd.set_seed(savedSeed);
		tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &sourceIndiv, true);
		}
	treeRejectionThreshold = -10000;
	for(int i=0;i<1;i++){
		savedSeed = rnd.seed();
		optCalcs = 0;
		tempIndiv.treeStruct->SPRMutate(cutnum, broke, optPrecision, 0);
		var << tempIndiv.treeStruct->lnL << "\t" << optCalcs << "\t";
		optCalcs = 0;
		rnd.set_seed(savedSeed);
		tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &sourceIndiv, true);
		}
*/
/*	for(int i=0;i<1;i++){
		treeRejectionThreshold = origThresh;
		savedSeed = rnd.seed();
		optCalcs = 0;
		tempIndiv.treeStruct->SPRMutate(cutnum, broke, optPrecision, 0);
		var << tempIndiv.treeStruct->lnL << "\t" << optCalcs << "\t";
		optCalcs = 0;
		rnd.set_seed(savedSeed);
		tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &sourceIndiv, true);
		}
*/
	treeRejectionThreshold = origThresh;
	tempIndiv.treeStruct->RemoveTreeFromAllClas();
	delete tempIndiv.treeStruct;
	tempIndiv.treeStruct=NULL;
	sourceIndiv.treeStruct=NULL;
	optCalcs = 0;
	SPRMutate(cutnum, broke, optPrecision, 0);
	var << lnL << "\t" << optCalcs << "\n";
	optCalcs = 0;
#endif
	return 1;
	}


// 7/21/06 This function is now called by TopologyMutator to actually do the rearrangement
//It has the cut and broken nodenums passed in.  It also does NNI's
int Tree::SPRMutate(int cutnum, ReconNode *broke, FLOAT_TYPE optPrecision, int subtreeNode){
	//if the optPrecision passed in is < 0 it means that we're just trying to
	//make the tree structure for some reason, but don't have CLAs allocated
	//and don't intend to do blen opt
	bool createTopologyOnly=false;
	if(optPrecision < 0.0) createTopologyOnly=true;

	TreeNode* cut = allNodes[cutnum];
	TreeNode *broken = allNodes[broke->nodeNum];
	TreeNode *connector=NULL;
	TreeNode *sib;
	//note that this assignment of the sib can be overridden below if cut is attached to the root or the subtreeNode
	if(cut->next!=NULL) sib=cut->next;
	else sib=cut->prev;

	//determine who the connector node will be.  It will be cut->anc unless that is the root
	//if cut->anc is the root, connector will be one of cut's siblings, which is freed when
	//the basal trichotomy is reestablished after removing cut.
	if(cut->anc->IsNotRoot()){
		if(cut->anc->nodeNum != subtreeNode){
			connector=cut->anc;
			}
		else{
			//cut is attached to the subtreeNode, so we will have to use it's sib as the connector
			connector=sib;
			sib=connector->left;
			}
		}
	else{
		if(root->left!=cut && root->left->IsInternal()) connector = root->left;
		else if(root->left->next!=cut && root->left->next->IsInternal()) connector = root->left->next;
		else if(root->right!=cut && root->right->IsInternal()) connector = root->right;
		else{//this should be quite rare, and means that the three descendents of the root
			//are cut and two terminals, so no viable swap exists, just try again
			return -1;
			}
		}

	//all clas below cut will need to be recalced
	if(createTopologyOnly == false) SweepDirtynessOverTree(cut);
	TreeNode *replaceForConn;
	if(cut->anc->anc){
		if(cut->anc->nodeNum != subtreeNode){
			//cut is not connected to the root, so we can steal it's ancestor as the new connector
		   	if(cut==connector->left){
		   		assert(cut->next==connector->right);
				replaceForConn=connector->right;
		   		}
		   	else{
		   		assert(cut==connector->right);
		   		replaceForConn=connector->left;
		   		}
			SetBranchLength(replaceForConn, min(max_brlen, replaceForConn->dlen+connector->dlen));
			connector->SubstituteNodeWithRespectToAnc(replaceForConn);
		   	}
		else{//cut is attached to the subtreeNode, so we will have to use it's sib as the connector
			//connector's two children become the subtreeNodes new children, and connector's dlen gets added to subtreeNodes
			TreeNode *subnode=allNodes[subtreeNode];
			SetBranchLength(subnode, min(max_brlen, subnode->dlen+connector->dlen));
			SweepDirtynessOverTree(connector);
			subnode->left=connector->left;
			subnode->right=connector->right;
			connector->left->anc=subnode;
			connector->right->anc=subnode;
			}
		}
	else{//cut is connected to the root so we need to steal a non terminal sib node as the connector
		if(createTopologyOnly == false) MakeNodeDirty(root);
		//Disconnect cut from the root
		if(cut==root->left){
			root->left=cut->next;
			cut->next->prev=NULL;
			}
		else if(cut==root->right){
			root->right=cut->prev;
			cut->prev->next=NULL;
			}
		else{
			assert(cut->prev==root->left && cut->next==root->right);//can only have a basal trifucation, or we're in trouble
			cut->prev->next=cut->next;
			cut->next->prev=cut->prev;
			}
		//root is now bifurcation
		//preserve branch length info
		if(root->right==connector){
			SetBranchLength(root->left, min(max_brlen, root->left->dlen+connector->dlen));
			sib=root->left;
			}
		else{
			SetBranchLength(root->right, min(max_brlen, root->right->dlen+connector->dlen));
			sib=root->right;
			}

		//add the connectors two desccendants as descendants of the root
		assert(connector->right==connector->left->next);
		connector->SubstituteNodeWithRespectToAnc(connector->left);
		root->AddDes(connector->right);
		}

	//establish correct topology for connector and cut nodes
	if(createTopologyOnly == false) MakeNodeDirty(connector);
	cut->anc=connector;
	connector->left=connector->right=cut;
	connector->next=connector->prev=connector->anc=cut->next=cut->prev=NULL;

	broken->SubstituteNodeWithRespectToAnc(connector);
	connector->AddDes(broken);
	assert(connector->right == broken);

	SetBranchLength(connector, max(min_brlen, broken->dlen*ZERO_POINT_FIVE));
	SetBranchLength(broken, connector->dlen);

	if(createTopologyOnly == false){
		SweepDirtynessOverTree(connector, cut);
		if(broke->reconDist > 1)
			OptimizeBranchesWithinRadius(connector, optPrecision, subtreeNode, sib);
		else
			OptimizeBranchesWithinRadius(connector, optPrecision, subtreeNode, NULL);
		}
	bipartCond = DIRTY;
	return 0;
}

void Tree::ReorientSubtreeSPRMutateDummy(int oroot, ReconNode *nroot, FLOAT_TYPE optPrecision){
	//this is just a spoof version of SPRMutate that will perform the same mutation
	//several times with different optimiation settings, but will otherwise
	//maintain exactly the same program flow because it resets the seed
#ifndef VARIABLE_OPTIMIZATION
	assert(0);
#else
	Individual tempIndiv;
	tempIndiv.treeStruct=new Tree();

	Individual sourceIndiv;
	sourceIndiv.treeStruct=this;
	sourceIndiv.mod->CopyModel(this->mod);

	int savedSeed;
	var.precision(10);
	var << "reSPR" << "\t" << nroot->reconDist << "\t" << lnL << "\t";
	//FLOAT_TYPE prec[5]={(FLOAT_TYPE).01, (FLOAT_TYPE).5, (FLOAT_TYPE).01, (FLOAT_TYPE).01, (FLOAT_TYPE).01};

	tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &sourceIndiv);
	FLOAT_TYPE origThresh = treeRejectionThreshold;
/*
	treeRejectionThreshold = 10000;

	for(int i=0;i<1;i++){
		savedSeed = rnd.seed();
		optCalcs = 0;
		tempIndiv.treeStruct->ReorientSubtreeSPRMutate(oroot, nroot, optPrecision);
		var << tempIndiv.treeStruct->lnL << "\t" << optCalcs << "\t";
		optCalcs = 0;
		rnd.set_seed(savedSeed);
		tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &sourceIndiv, true);
		}
	treeRejectionThreshold = -10000;
	for(int i=0;i<1;i++){
		savedSeed = rnd.seed();
		optCalcs = 0;
		tempIndiv.treeStruct->ReorientSubtreeSPRMutate(oroot, nroot, optPrecision);
		var << tempIndiv.treeStruct->lnL << "\t" << optCalcs << "\t";
		optCalcs = 0;
		rnd.set_seed(savedSeed);
		tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &sourceIndiv, true);
		}
*/
		/*	for(int i=0;i<1;i++){
		treeRejectionThreshold = origThresh;
		savedSeed = rnd.seed();
		optCalcs = 0;
		tempIndiv.treeStruct->ReorientSubtreeSPRMutate(oroot, nroot, optPrecision);
		var << tempIndiv.treeStruct->lnL << "\t" << optCalcs << "\t";
		optCalcs = 0;
		rnd.set_seed(savedSeed);
		tempIndiv.CopySecByRearrangingNodesOfFirst(tempIndiv.treeStruct, &sourceIndiv, true);
		}
*/
	treeRejectionThreshold = origThresh;
	tempIndiv.treeStruct->RemoveTreeFromAllClas();
	delete tempIndiv.treeStruct;
	tempIndiv.treeStruct=NULL;
	sourceIndiv.treeStruct=NULL;
	optCalcs = 0;
	ReorientSubtreeSPRMutate(oroot, nroot, optPrecision);
	var << lnL << "\t" << optCalcs << "\n";
	optCalcs = 0;
#endif
	}

void Tree::ReorientSubtreeSPRMutate(int oroot, ReconNode *nroot, FLOAT_TYPE optPrecision){
	//this is used to allow the other half of SPR rearrangements in which
	//the part of the tree containing the root is considered the subtree
	//to be attached.  Terminology is VERY confusing here. newRoot is the
	//branch to be bisected (rooted at).  oldRoot is the node that is at the
	//base of the subtree currently.  After the rearrangement it will still
	//be at the base of the subtree, but in the middle of a different branch

	//if the optPrecision passed in is < 0 it means that we're just trying to
	//make the tree structure for some reason, but don't have CLAs allocated
	//and don't intend to do blen opt
	bool createTopologyOnly=false;
	if(optPrecision < 0.0) createTopologyOnly=true;

	TreeNode *newroot=allNodes[nroot->nodeNum];
	TreeNode *oldroot=allNodes[oroot];

	//these are the only blens that need to be dealt with specially
	FLOAT_TYPE fusedBlen = min(max_brlen, oldroot->left->dlen + oldroot->right->dlen);
	FLOAT_TYPE dividedBlen = max(ZERO_POINT_FIVE * newroot->dlen, min_brlen);

	//first detatch the subtree and make it free floating.  This will
	//leave oroot in its place and fuse two branches in the subtree
	//into a branch connecting one of oroots des to its other des
	//This makes that des a tricotomy with a NULL anc.  Then the rotating
	//begins.
	if(createTopologyOnly == false){
		SweepDirtynessOverTree(oldroot->left);
		SweepDirtynessOverTree(oldroot->right);
		}

	TreeNode *prunePoint;
	TreeNode *tempRoot;
	if(oldroot->left->IsInternal()){
		tempRoot=oldroot->left;
		prunePoint=oldroot->right;
		}
	else{
		tempRoot=oldroot->right;
		prunePoint=oldroot->left;
		}

	tempRoot->AddDes(prunePoint);
	//prunePoint->dlen=fusedBlen;
	SetBranchLength(prunePoint, fusedBlen);
	tempRoot->anc=NULL;

	if(createTopologyOnly == false) MakeNodeDirty(tempRoot);

	//collect each of the nodes that will need to be flipped
	vector<TreeNode *> path;
	path.reserve(10);
	TreeNode *tmp=newroot->anc;
	while(tmp){
		path.push_back(tmp);
		tmp=tmp->anc;
		}
	reverse(path.begin(),path.end());

	for(vector<TreeNode*>::iterator it=path.begin();(it+1)!=path.end();it++){
		(*it)->MoveDesToAnc(*(it+1));
		}

	//now disconnect the oldroot
	oldroot->left = NULL;
	oldroot->right = NULL;

	//and add the new des
	TreeNode *oldanc=newroot->anc;
	oldanc->RemoveDes(newroot);
	oldroot->AddDes(oldanc);
	oldroot->AddDes(newroot);

	SetBranchLength(oldroot->left, dividedBlen);
	SetBranchLength(oldroot->right, dividedBlen);

	if(createTopologyOnly == false){
		SweepDirtynessOverTree(newroot);
		SweepDirtynessOverTree(oldroot);
		SweepDirtynessOverTree(tempRoot);
		SweepDirtynessOverTree(prunePoint);
		if(nroot->reconDist > 1) OptimizeBranchesWithinRadius(oldroot, optPrecision, 0, prunePoint);
		else OptimizeBranchesWithinRadius(oldroot, optPrecision, 0, NULL);
		}
	bipartCond = DIRTY;
	}




				
void Tree::ReadNewickConstraint(const char * newick, bool numericalTaxa, bool isPositive) {				
	//this is rather silly, but because any call to the Tree constructor will generate random
	//branch lengths (even though this tree is only temporary for the reading of the constraint),
	//it will change the seed.  So, store and restore it
	int seed = rnd.seed();
	//the last two arguments here specify that both polytomies and missing taxa (for backbone constraints) should be allowed
	Tree contree(newick, numericalTaxa, true, true);
	//check if the tree is completely constrained - users try to do that to optimize on a fixed
	//topology, but that should be done by specifying a starting tree and a topoweight of zero
	if(contree.numNodesAdded == contree.allNodes.size())
		throw ErrorException("Constraint represents a fully resolved tree!\nIf you would like to fix the tree topology during a run,\ndo so by specifying your tree as a starting tree and\nsetting topoweight to 0.0");

	rnd.set_seed(seed);

	contree.CalcBipartitions(true);
	vector<Bipartition> bip;
	contree.root->GatherConstrainedBiparitions(bip);
	if(bip.size() == 0)
		throw ErrorException("Specified constraint does not constrain any relationships.\n\tSee manual for constraint format");
	if( (!isPositive) && (bip.size() > 1))
		throw ErrorException("Sorry, GARLI can currently only handle a single negatively (conversely) constrainted branch (bipartition):-(");

	//BACKBONE - see if all taxa appear in this constraint or if its a backbone
	if(contree.numTipsAdded < contree.numTipsTotal) {
		Bipartition mask = contree.root->bipart;
		//complement the mask if necessary
		TreeNode *n = contree.root;
		while(n->IsInternal())
			n = n->left;
		if (!mask.ContainsTaxon(n->nodeNum))
			mask.Complement();

		for(vector<Bipartition>::iterator bit=bip.begin();bit!=bip.end();bit++) {
			const Constraint constraint(*bit, mask, isPositive);
			Tree::AddConstraint(constraint);
		}
	}
	else {
		for(vector<Bipartition>::iterator bit=bip.begin();bit!=bip.end();bit++) {
			const Constraint constraint(*bit, isPositive);
			Tree::AddConstraint(constraint);
		}
	}
}


//this just "fakes" the swapping of the subtree rooted at cut to a postition as the sister of broken by adjusting the
//biparts across the tree.  This should only be used for NORMAL SPR's not subtree reorient SPR's
void Tree::AdjustBipartsForSwap(int cut, int broken){
	//first be sure the biparts are current
	CalcBipartitions(true);
	if(allNodes[cut]->anc->IsNotRoot()) allNodes[cut]->anc->RecursivelyAddOrRemoveSubtreeFromBipartitions(allNodes[cut]->bipart);
	if(allNodes[broken]->anc->IsNotRoot()) allNodes[broken]->anc->RecursivelyAddOrRemoveSubtreeFromBipartitions(allNodes[cut]->bipart);
	bipartCond = TEMP_ADJUSTED;
	}

//test whether the attachment of branch "cut" (subtree or tip) to branch "broken" (subtree or tip) is allowed by
//any constraints.  The general purpose Constraint::BipartitionIsCompatibleWithConstraint function (which takes care of
//positive and negative constraints, backbone or not) is called to check if the bipartition created by the union
//of cut and broken is itself allowable.  Depending on the type of constraint, other checks may also need to be done.
bool Tree::SwapAllowedByConstraint(const Constraint &constr, TreeNode *cut, ReconNode *broken, const Bipartition &proposed, const Bipartition *partialMask) {
	//for a normal positive constraint with no mask we only need to check the bipartition about to be created
	if(constr.IsPositive() && !constr.IsBackbone() && partialMask==NULL)
		return constr.BipartitionIsCompatibleWithConstraint(proposed, NULL);
	else{
		//otherwise we need to check bipartitions across the tree
		bool compat;
		/*(check for meaningful intersection of constraint and partial/backbone mask here)*/
		Bipartition jointMask;
		bool meaningfulIntersection = jointMask.MakeJointMask(constr, partialMask);
		if(!meaningfulIntersection) return true;

		if(!broken->withinCutSubtree){
			//if this is a normal SPR swap in which the cut subtree has the same orientation after the swap then we can
			//check the bipartition about to be created, and if that passes then adjust the bipartitions across the tree and
			//recursively check the rest of the tree
			compat = constr.BipartitionIsCompatibleWithConstraint(proposed, &jointMask);
			if(compat == false) return compat;

			//this screws up the biparitions, so they need to be recalculated before returning
			AdjustBipartsForSwap(cut->nodeNum, broken->nodeNum);

			compat = RecursiveAllowedByConstraintWithMask(constr, &jointMask, root);
			}
		else{
			Tree propTree;
			propTree.MimicTopo(this);
			propTree.ReorientSubtreeSPRMutate(cut->nodeNum, broken, -1.0);

			compat = (constr.IsPositive()) == (propTree.ContainsMaskedBipartitionOrComplement(constr.GetBipartition(), jointMask) != NULL);
			}
		return compat;
		}
	}
/*
bool Tree::TaxonAdditionAllowedByPositiveConstraintWithMask(Constraint *constr, Bipartition *mask, TreeNode *toAdd, TreeNode *broken){
	Bipartition proposed;
	proposed.FillWithXORComplement(toAdd->bipart, broken->bipart);

	bool compat = constr->BipartitionIsCompatibleWithConstraint(&proposed, mask);

	if(compat==false) return compat;
	//This is a little sneaky here.  Cut has not been added to the tree, but since we are going up from broken
	//and it is present in the mask it will effectively appear in biparts in that direction
	else if(broken->IsInternal()){
		compat=RecursiveAllowedByConstraintWithMask(constr, mask, broken);
		}
	return compat;
	}

bool Tree::TaxonAdditionAllowedByNegativeConstraintWithMask(Constraint *constr, Bipartition *mask, TreeNode *toAdd, TreeNode *broken){
	Bipartition proposed;
	proposed.FillWithXORComplement(toAdd->bipart, broken->bipart);

	bool compat = constr->BipartitionIsCompatibleWithConstraint(&proposed, mask);
	if(compat==true) return compat;
	else if(broken->IsInternal()) compat=RecursiveAllowedByConstraintWithMask(constr, mask, broken);
	return compat;
	}

bool Tree::TaxonAdditionAllowedByPositiveBackboneConstraintWithMask(Constraint *constr, Bipartition *mask, TreeNode *toAdd, TreeNode *broken){
	Bipartition proposed;
	proposed.FillWithXORComplement(toAdd->bipart, broken->bipart);

	bool compat = constr->BipartitionIsCompatibleWithConstraint(&proposed, mask);
	if(compat==false) return compat;
	else{
		if(broken->anc->IsNotRoot()) broken->anc->RecursivelyAddOrRemoveSubtreeFromBipartitions(toAdd->bipart);
		bipartCond = TEMP_ADJUSTED;

		compat = RecursiveAllowedByConstraintWithMask(constr, mask, root);
		CalcBipartitions(false);
		}
	return compat;
	}

bool Tree::TaxonAdditionAllowedByNegativeBackboneConstraintWithMask(Constraint *constr, Bipartition *mask, TreeNode *toAdd, TreeNode *broken){
	//	Bipartition jointMask=*(constr->GetBackboneMask());
//	jointMask.AndEquals(mask);

	Bipartition proposed;
	proposed.FillWithXORComplement(toAdd->bipart, broken->bipart);
	//bool compat = constr->IsCompatibleWithConstraintWithMask(&proposed, &jointMask);
	bool compat = constr->BipartitionIsCompatibleWithConstraint(&proposed, mask);
	if(compat==false) return compat;
	else{
		if(broken->anc->IsNotRoot()) broken->anc->RecursivelyAddOrRemoveSubtreeFromBipartitions(toAdd->bipart);
		bipartCond = TEMP_ADJUSTED;

		compat = RecursiveAllowedByConstraintWithMask(constr, mask, root);
		CalcBipartitions(false);
		}
	return compat;
	}
*/
//This can be called with the root, and it then recurces through the tree until it finds a bipartition that conflicts
//with the constraint.  Unlike the ContainsBipartition functions, it doesn't actually require that the actual tree
//to be checked has been made (i.e. that the swap has been done) - just that the bipartitions have been altered
//as if it had.  It therefore has lower overhead when checking swaps and should be preferred.  The mask passed in should only be
//should include any backbone constraint and/or a mask containing those taxa present in a growing tree
bool Tree::RecursiveAllowedByConstraintWithMask(const Constraint &constr, const Bipartition *jointMask, const TreeNode *nd){
	bool compat = true;
	if(nd->IsNotRoot())
		compat = constr.BipartitionIsCompatibleWithConstraint(nd->bipart, jointMask);
	if(compat==false) return compat;

	if(nd->left->IsInternal()) compat=RecursiveAllowedByConstraintWithMask(constr, jointMask, nd->left);
	if(compat==false) return compat;

	if(nd->left->next->IsInternal()) compat=RecursiveAllowedByConstraintWithMask(constr, jointMask, nd->left->next);
	if(compat==false) return compat;

	if(nd->left->next->next != NULL)//this would be the right dec of the root
		if(nd->left->next->next->IsInternal())
			compat=RecursiveAllowedByConstraintWithMask(constr, jointMask, nd->left->next->next);

	return compat;
	}

//DJZ 8-11-04  This version is only for the master doing SPRs on nodes that aren't in a subtree when subtree
//mode is on.  Basically the only difference is that if the ancestor of the cut node is the root, we need to
//choose one of the other nonSubtree nodes to make a connector to avoid screwing up the subtree partitioning
void Tree::SPRMutate(int cutnum, int broknum, FLOAT_TYPE optPrecision, const vector<int> &nonSubNodes)
{	assert( numBranchesAdded > 3 );
	assert(0);//needst to be verified
	TreeNode* cut = allNodes[cutnum];
	assert(cut!=NULL);
	SweepDirtynessOverTree(cut->anc);
	TreeNode *connector;

	if(cut->anc->IsNotRoot()){
		connector=cut->anc;
		}
	else{
		bool foundAConn=false;
		connector=cut->prev;
		while(connector && !foundAConn)//try previous sibs
			{if(connector->left && find(nonSubNodes.begin(),nonSubNodes.end(),connector->nodeNum)!=nonSubNodes.end())//not a terminal
				foundAConn=true;
			else
				connector=connector->prev;
			}
		if(!foundAConn)
			{connector=cut->next;//that didn't work try the next sibs
			while(connector && !foundAConn)//try previous sibs
				{if(connector->left && find(nonSubNodes.begin(),nonSubNodes.end(),connector->nodeNum)!=nonSubNodes.end())//not a terminal
					foundAConn=true;
				else
					connector=connector->next;
				}
			}
		if(!foundAConn)
			return;//oops by chance we picked a trivial branch to cut, so it goes (if you want to call SPRMutate again that would make sure the tree always changes topo
		}

	SweepDirtynessOverTree(cut);
	TreeNode *replaceForConn;
	if(cut->anc->anc){
		//cut is not connected to the root, so we can steal it's ancestor as the new connector
	   	if(cut==connector->left){
	   		replaceForConn=connector->right;
	   		}
	   	else{
	   		replaceForConn=connector->left;
	   		}
		replaceForConn->dlen+=connector->dlen;
	   	connector->SubstituteNodeWithRespectToAnc(replaceForConn);
	   	}
	else{//cut is connected to the root so we need to steal a non terminal sib node as the connector
		//this makes the root totally dirty
		MakeNodeDirty(root);

		//Disconnect cut from the root
		if(cut==root->left){
			root->left=cut->next;
			cut->next->prev=NULL;
			}
		else if(cut==root->right){
			root->right=cut->prev;
			cut->prev->next=NULL;
			}
		else{
			assert(cut->prev==root->left && cut->next==root->right);//can only have a basal trifucation, or we're in trouble
			cut->prev->next=cut->next;
			cut->next->prev=cut->prev;
			}
		//root is now bifurcation
		//preserve branch length info
		if(root->right==connector)
			root->left->dlen+=	connector->dlen;
		else
			root->right->dlen+=	connector->dlen;
		//add the connectors two desccendants as descendants of the root
		assert(connector->right==connector->left->next);
		connector->SubstituteNodeWithRespectToAnc(connector->left);
		root->AddDes(connector->right);
		MakeNodeDirty(connector);
		}

	//establish correct topology for connector and cut nodes
	cut->anc=connector;
	connector->left=connector->right=cut;
	connector->next=connector->prev=connector->anc=cut->next=cut->prev=NULL;

	TreeNode *broken=allNodes[broknum];

	broken->SubstituteNodeWithRespectToAnc(connector);
	connector->AddDes(broken);

	if(broken->dlen*ZERO_POINT_FIVE > min_brlen){
		connector->dlen=broken->dlen*ZERO_POINT_FIVE;
		broken->dlen-=connector->dlen;
		}
	else connector->dlen=broken->dlen=min_brlen;

	SweepDirtynessOverTree(connector, cut);
	MakeNodeDirty(connector);

#ifdef OPT_DEBUG
	opt << "SPR\n";
#endif
	OptimizeBranchesWithinRadius(connector, optPrecision, 0, NULL);
	bipartCond = DIRTY;
	}

TreeNode * getCorrespondingNode(const TreeNode * s, std::vector<TreeNode *> & an);

inline TreeNode * getCorrespondingNode(const TreeNode * s, std::vector<TreeNode *> & an)
{
	return (s == NULL ? NULL : an[s->nodeNum]); 
}

void Tree::MimicTopo(const Tree *source){
//DZ 10-25-02 This should be much easier and faster using the allnodes array rather
//than being recursive.  Notice that even if the allNodes array of source is not
//ordered according to nodeNum, the new tree will be.
	const std::vector<TreeNode *> &allNs = source->allNodes;
	for(int i=0;i<source->allNodes.size();i++) {
		TreeNode * srcNd = allNs[i];
		allNodes[i]->anc = getCorrespondingNode(srcNd->anc, allNodes);
		allNodes[i]->left = getCorrespondingNode(srcNd->left, allNodes);
		allNodes[i]->right = getCorrespondingNode(srcNd->right, allNodes);
		allNodes[i]->next = getCorrespondingNode(srcNd->next, allNodes);
		allNodes[i]->prev = getCorrespondingNode(srcNd->prev, allNodes);
		allNodes[i]->dlen=allNs[i]->dlen;
		allNodes[i]->SetAttached(srcNd->IsAttached());
		}
	numNodesAdded=source->numNodesAdded;
	numTipsAdded=source->numTipsAdded;
	numBranchesAdded=source->numBranchesAdded;
	bipartCond = DIRTY;
	}

//this version is used for just copying a subtree,
//but assumes that the nodenums will match.  Automatically
//copys the cla indeces too
void Tree::MimicTopo(const TreeNode *nd, bool firstNode, bool sameModel){
	//firstNode will be true if this is the base of the subtree to be copied.
	//if it is true, the anc, next and prev should not be copied for that node
	//Above the firstNode, nodes will be assumed to be the same nodenum in both trees.  This
	//allows replicating nodeNums from a certain subtree up, but not in the rest of the tree
	//The cla info will only be copied if the models are identical for the individuals (sameModel==true)
	//otherwise the replicated nodes will be marked as dirty
	TreeNode *mnd;
	mnd=allNodes[nd->nodeNum];
	mnd->SetAttached(true);
	if(!firstNode){
		//stuff that should not be done for the root of the subtree
		if(nd->anc){
			mnd->anc=allNodes[nd->anc->nodeNum];
			}
		else{
			mnd->anc=NULL;
			}
		if(nd->next){
			mnd->next=allNodes[nd->next->nodeNum];
				MimicTopo(nd->next, false, sameModel);
			}
		else
			mnd->next=NULL;
		if(nd->prev){
			mnd->prev=allNodes[nd->prev->nodeNum];
			}
		else
			mnd->prev=NULL;
		}
	//this should apply to all nodes
	if(nd->left){ //if this is not a terminal
		mnd->left=allNodes[nd->left->nodeNum];
		mnd->right=allNodes[nd->right->nodeNum];
		MimicTopo(nd->left, false, sameModel);
		}
	else
		mnd->right=mnd->left=NULL;;

	//the clas are now taken care of back where this was called
/*	if(nd->left){
		if(sameModel==true)
			mnd->CopyOneClaIndex(nd, claMan, DOWN);
		else mnd->claIndexDown=claMan->SetDirty(mnd->claIndexDown);
		}
*/
	mnd->dlen=nd->dlen;
	bipartCond = DIRTY;
}

void Tree::CopyClaIndecesInSubtree(const TreeNode *from, bool remove){
	//the bool argument "remove" designates whether the tree currently has cla arrays
	//assigned to it or not (if not, it must have come from the unused tree vector)
	//note that we assume that the node numbers and topologies match within the subtree
	assert(from->anc);

	//do the clas down
	if(remove) claMan->DecrementCla(allNodes[from->nodeNum]->claIndexDown);
	allNodes[from->nodeNum]->claIndexDown=from->claIndexDown;
	if(allNodes[from->nodeNum]->claIndexDown != -1) claMan->IncrementCla(allNodes[from->nodeNum]->claIndexDown);

	//do the clas up left
	if(remove) claMan->DecrementCla(allNodes[from->nodeNum]->claIndexUL);
	allNodes[from->nodeNum]->claIndexUL=from->claIndexUL;
	if(allNodes[from->nodeNum]->claIndexUL != -1) claMan->IncrementCla(allNodes[from->nodeNum]->claIndexUL);

	//do the clas up right
	if(remove) claMan->DecrementCla(allNodes[from->nodeNum]->claIndexUR);
	allNodes[from->nodeNum]->claIndexUR=from->claIndexUR;
	if(allNodes[from->nodeNum]->claIndexUR != -1) claMan->IncrementCla(allNodes[from->nodeNum]->claIndexUR);

	if(from->left->IsInternal())
		CopyClaIndecesInSubtree(from->left, remove);
	if(from->right->IsInternal())
		CopyClaIndecesInSubtree(from->right, remove);
	}

void Tree::DirtyNodesInSubtree(TreeNode *nd){

	MakeNodeDirty(nd);
	if(nd->left->IsInternal()) DirtyNodesInSubtree(nd->left);
	if(nd->right->IsInternal()) DirtyNodesInSubtree(nd->right);

	}

void Tree::RescaleRateHet(CondLikeArray *destCLA){

		FLOAT_TYPE *destination=destCLA->arr;
		int *underflow_mult=destCLA->underflow_mult;
		const int *c=data->GetCounts();
		const int nsites = data->NChar();
		const int nRateCats = mod->NRateCats();

		//check if any clas are getting close to underflow
#ifdef UNIX
		madvise(destination, sizeof(FLOAT_TYPE)*4*nRateCats*nsites, MADV_SEQUENTIAL);
		madvise(underflow_mult, sizeof(int)*nsites, MADV_SEQUENTIAL);
#endif
		FLOAT_TYPE large1, large2;
		for(int i=0;i<nsites;i++){
#ifdef USE_COUNTS_IN_BOOT
			if(c[i] > 0){
#else
			if(1){
#endif
//for some reason optimzation in gcc 2.95 breaks the more optimal version of this code
//this version is safer
#if defined(__GNUC__) && __GNUC__ < 3
				small1 = FLT_MAX;
				large1 = FLT_MIN;
				for(int r=0;r<nRateCats;r++){
					large2= max(destination[4*r+0] , destination[4*r+1]);
					large2 = max(large2 , destination[4*r+2]);
					large2 = max(large2 , destination[4*r+3]);
					large1 = max(large1, large2);
					}

#else
	#if (defined(_MSC_VER) || defined(__INTEL_COMPILER)) && !defined(SINGLE_PRECISION_FLOATS)
			//This is a neat trick for quickly finding the approximately largest
			//value of an array of doubles, but it only works on littleendian
			//systems.  There's no easy way of detecting endianness at compile
			//time that I've been able to find, but since x86 machines are always
			//littleendian, this should be safe
				int size = 4 * nRateCats;
				unsigned int absvalue, largest_abs = 0;
				for (int j = 0; j < size; j++) {
					// Get upper 32 bits of a[i] and shift out sign bit:
					absvalue = *((unsigned int*)&destination[j] + 1) * 2;
					// Find numerically largest element (approximately):
					if (absvalue > largest_abs) {
						largest_abs = absvalue;
						large1 = destination[j];
						}
					}
	#else
				large1= (destination[0] > destination[2] ? destination[0] : destination[2]);
				large2= (destination[1] > destination[3] ? destination[1] : destination[3]);
				large1= (large1 > large2 ? large1 : large2);

				for(int r=1;r<nRateCats;r++){
					large2= (destination[0 + r*4] > destination[2 + r*4] ? destination[0 + r*4] : destination[2 + r*4]);
					large1= (large1 > large2 ? large1 : large2);
					large2= (destination[1 + r*4] > destination[3 + r*4] ? destination[1 + r*4] : destination[3 + r*4]);
					large1= (large1 > large2 ? large1 : large2);
					}
	#endif
#endif
#ifdef SINGLE_PRECISION_FLOATS
				assert(large1 < 1e5);
				if(large1 < rescaleBelow){
					if(large1 < 1e-30f){//DEBUG
						outman.UserMessage("RESCALE REDUCED");
						throw(1);
						}
					int index=0;
					while(((index + 1) < 30) && (Tree::rescalePrecalcThresh[index + 1] > large1)){
						index++;
						}
					int incr = Tree::rescalePrecalcIncr[index];
					underflow_mult[i]+=incr;
					FLOAT_TYPE mult=Tree::rescalePrecalcMult[index];
					assert(large1 * mult < 1.0f);
					assert(large1 * mult > 0.01f);
					for(int r=0;r<nRateCats;r++){
						for(int q=0;q<4;q++){
							destination[r*4 + q]*=mult;
							assert(destination[r*4 +q] == destination[r*4 +q]);
							assert(destination[r*4 +q] < 1);
							}
						}
					}
#else	//double precison
				if(large1< rescaleBelow){
					if(large1 < 1e-190){
						throw(1);
						}
					int index = 0;
					while(((index + 1) < RESCALE_ARRAY_LENGTH) && (Tree::rescalePrecalcThresh[index + 1] > large1)){
						index++;
						}
					int incr = Tree::rescalePrecalcIncr[index];
					underflow_mult[i]+=incr;
					FLOAT_TYPE mult=Tree::rescalePrecalcMult[index];
					assert(large1 * mult < 1.0);
					assert(large1 * mult > 0.0008);

					for(int r=0;r<nRateCats;r++){
						for(int q=0;q<4;q++){
							destination[r*4 + q]*=mult;
							assert(destination[r*4 +q] == destination[r*4 +q]);
							assert(destination[r*4 +q] < 1e50);
							}
						}
					}
#endif //end of ifdef(SINGLE_PRECISION_FLOATS)

				destination+= 4*nRateCats;
	#ifdef ALLOW_SINGLE_SITE
				if(siteToScore > -1) break;
	#endif
				}
			else{
	#ifdef OPEN_MP
			//this is a little strange, but dest only needs to be advanced in the case of OMP
			//because sections of the CLAs corresponding to sites with count=0 are skipped
			//over in OMP instead of being eliminated
				destination += 4 * nRateCats;
	#endif
				}
			}

		destCLA->rescaleRank=0;
		}

void Tree::RescaleRateHetNState(CondLikeArray *destCLA){

	FLOAT_TYPE *destination=destCLA->arr;
	int *underflow_mult=destCLA->underflow_mult;

	const int nsites = data->NChar();
	const int nstates = mod->NStates();
	const int nRateCats = mod->NRateCats();
	const int *c = data->GetCounts();

	//check if any clas are getting close to underflow
#ifdef UNIX
	madvise(destination, sizeof(FLOAT_TYPE)*nstates*nRateCats*nsites, MADV_SEQUENTIAL);
	madvise(underflow_mult, sizeof(int)*nsites, MADV_SEQUENTIAL);
#endif
	FLOAT_TYPE large1;
	for(int i=0;i<nsites;i++){
#ifdef USE_COUNTS_IN_BOOT
		if(c[i] > 0){
#else
		if(1){
#endif

#if (defined(_MSC_VER) || defined(__INTEL_COMPILER)) && !defined(SINGLE_PRECISION_FLOATS)
			//This is a neat trick for quickly finding the approximately largest
			//value of an array of doubles, but it only works on littleendian
			//systems.  There's no easy way of detecting endianness at compile
			//time that I've been able to find, but since x86 machines are always
			//littleendian, this should be safe
			int size = nstates * nRateCats;
			unsigned int absvalue, largest_abs = 0;
			for (int j = 0; j < size; j++) {
				// Get upper 32 bits of a[i] and shift out sign bit:
				absvalue = *((unsigned int*)&destination[j] + 1) * 2;
				// Find numerically largest element (approximately):
				if (absvalue > largest_abs) {
					largest_abs = absvalue;
					large1 = destination[j];
					}
				}
#else

			large1 = (destination[0] > destination[1]) ? destination[0] :  destination[1];
			for(int s=2;s<nstates*nRateCats;s++){
				large1 = (destination[s] > large1) ? destination[s] : large1;
				}
#endif

#ifdef SINGLE_PRECISION_FLOATS
			assert(large1 < 1.0e10f);
			if(large1< rescaleBelow){
				if(large1 < 1e-30f){
					outman.UserMessage("RESCALE REDUCED");
					throw(1);
					}
				int index=0;

				while(((index + 1) < 30) && (Tree::rescalePrecalcThresh[index + 1] > large1)){
					index++;
					}

				int incr = Tree::rescalePrecalcIncr[index];
				underflow_mult[i]+=incr;
				FLOAT_TYPE mult= Tree::rescalePrecalcMult[index];
				assert(large1 * mult < 10.0f);
				assert(large1 * mult > 0.01f);
				for(int q=0;q<nstates*nRateCats;q++){
					destination[q]*=mult;
					assert(destination[q] == destination[q]);
					}
				}
#else
			assert(large1 < 1.0e15);
			if(large1< rescaleBelow){
				if(large1 < 1e-190){
					throw(1);
					}
				int index = 0;
				while(((index + 1) < RESCALE_ARRAY_LENGTH) && (Tree::rescalePrecalcThresh[index + 1] > large1)){
					index++;
					}

				int incr = Tree::rescalePrecalcIncr[index];
				underflow_mult[i]+=incr;
				FLOAT_TYPE mult=Tree::rescalePrecalcMult[index];
				assert(large1 * mult < 1.0);
				assert(large1 * mult > 0.0008);

				for(int q=0;q<nstates*nRateCats;q++){
					destination[q]*=mult;
					assert(destination[q] == destination[q]);
					assert(destination[q] < 1.0e5);
					}
				}
#endif
			destination+= nstates*nRateCats;
#ifdef ALLOW_SINGLE_SITE
			if(siteToScore > -1) break;
#endif
			}
		else{
#ifdef OPEN_MP
			//this is a little strange, but dest only needs to be advanced in the case of OMP
			//because sections of the CLAs corresponding to sites with count=0 are skipped
			//over in OMP instead of being eliminated
			destination += nstates * nRateCats;
#endif
			}
		}

	destCLA->rescaleRank=0;
	}

int Tree::ConditionalLikelihoodRateHet(int direction, TreeNode* nd, bool fillFinalCLA /*=false*/){
	//note that fillFinalCLA just refers to whether we actually want to calc a CLA
	//representing the contribution of the entire tree vs just calcing the score
	//The only reason I can think of for doing that is to calc internal state probs
	//the fuction will then return a pointer to the CLA

	assert(this != NULL);
	calcCount++;

	CondLikeArray *destCLA=NULL;

	TreeNode* Lchild, *Rchild;
	CondLikeArray *LCLA=NULL, *RCLA=NULL, *partialCLA=NULL;

	FLOAT_TYPE *Rprmat = NULL, *Lprmat = NULL;
	FLOAT_TYPE blen1, blen2;

	if(direction != ROOT){
		//the only complicated thing here will be to set up the two children depending on the direction
		//get all of the clas, underflow mults and pmat set up here, then the actual calc loops below
		//won't depend on direction
		if(direction==DOWN){
			Lchild=nd->left;
			Rchild=nd->right;

			if(Lchild->IsInternal())
				LCLA=GetClaDown(Lchild);
			if(Rchild->IsInternal())
				RCLA=GetClaDown(Rchild);

			blen1 = Lchild->dlen;
			blen2 = Rchild->dlen;
			}
		else if(direction==UPRIGHT || direction==UPLEFT){
			if(nd->anc){
				Lchild=nd->anc;

				if(nd->anc->left==nd)
					LCLA=GetClaUpLeft(Lchild);

				else if(nd->anc->right==nd)
					LCLA=GetClaUpRight(Lchild);

				else//watch out here.  This is the case in which we want the cla at the root including the left
					//and right, but not the middle.  We will confusingly store this in the root's DOWN cla
					LCLA=GetClaDown(Lchild);

				blen1 = nd->dlen;

				if(direction==UPRIGHT) Rchild=nd->left;
				else Rchild=nd->right;
				}
			else{
				if(direction==UPRIGHT){
					Lchild=nd->left;
					Rchild=nd->left->next;
					}
				else{
					Lchild=nd->left->next;
					Rchild=nd->right;
					}
				if(Lchild->IsInternal())
					LCLA=GetClaDown(Lchild);

				blen1 = Lchild->dlen;
				}

			if(Rchild->IsInternal())
				RCLA=GetClaDown(Rchild);

			blen2 = Rchild->dlen;
			}
		assert(mod);
		mod->CalcPmats(blen1, blen2, Lprmat, Rprmat);

		if(direction==DOWN) destCLA=GetClaDown(nd, false);
		else if(direction==UPRIGHT) destCLA=GetClaUpRight(nd, false);
		else if(direction==UPLEFT) destCLA=GetClaUpLeft(nd, false);

		if(LCLA!=NULL && RCLA!=NULL){
			//two internal children
			ProfIntInt.Start();
#ifdef EQUIV_CALCS
			if(direction==DOWN){
				CalcFullCLAInternalInternalEQUIV(destCLA, LCLA, RCLA, &Lprmat[0], &Rprmat[0], nsites,  mod->NRateCats(), nd->left->tipData, nd->right->tipData);
				}
			else
#endif
			if(modSpec.IsNucleotide() == false)
				CalcFullCLAInternalInternalNState(destCLA, LCLA, RCLA, &Lprmat[0], &Rprmat[0]);
			else
				CalcFullCLAInternalInternal(destCLA, LCLA, RCLA, &Lprmat[0], &Rprmat[0]);
			ProfIntInt.Stop();
			}

		else if(LCLA==NULL && RCLA==NULL){
			//two terminal children
			ProfTermTerm.Start();
			if(modSpec.IsNucleotide() == false)
				CalcFullCLATerminalTerminalNState(destCLA, &Lprmat[0], &Rprmat[0], Lchild->tipData, Rchild->tipData);
			else
				CalcFullCLATerminalTerminal(destCLA, &Lprmat[0], &Rprmat[0], Lchild->tipData, Rchild->tipData);
			ProfTermTerm.Stop();
			}

		else{
			//one terminal, one internal
			ProfIntTerm.Start();

			if(modSpec.IsNucleotide() == false){
				if(LCLA==NULL)
					CalcFullCLAInternalTerminalNState(destCLA, RCLA, &Rprmat[0], &Lprmat[0], Lchild->tipData);
				else
					CalcFullCLAInternalTerminalNState(destCLA, LCLA, &Lprmat[0], &Rprmat[0], Rchild->tipData);
				}
			else{
#ifdef OPEN_MP
				if(LCLA==NULL)
					CalcFullCLAInternalTerminal(destCLA, RCLA, &Rprmat[0], &Lprmat[0], Lchild->tipData, Lchild->ambigMap);
				else
					CalcFullCLAInternalTerminal(destCLA, LCLA, &Lprmat[0], &Rprmat[0], Rchild->tipData, Rchild->ambigMap);
				}
#else
				if(LCLA==NULL)
					CalcFullCLAInternalTerminal(destCLA, RCLA, &Rprmat[0], &Lprmat[0], Lchild->tipData, NULL);
				else
					CalcFullCLAInternalTerminal(destCLA, LCLA, &Lprmat[0], &Rprmat[0], Rchild->tipData, NULL);
				}
#endif
			ProfIntTerm.Stop();
			}
		}

	if(direction==ROOT){
		//at the root we need to include the contributions of 3 branches.  Check if we have a
		//valid CLA that already represents two of these three. If so we can save a bit of
		//computation.  This will mainly be the case during blen optimization, when when we
		//only change one of the branches again and again.
		TreeNode *child;
		CondLikeArray *childCLA=NULL;

		if(claMan->IsDirty(nd->claIndexUL) == false){
			partialCLA=GetClaUpLeft(nd, false);
			child=nd->left;
			if(child->IsInternal()){
				childCLA=GetClaDown(child, true);
				}
			blen1 = child->dlen;
			}
		else if(claMan->IsDirty(nd->claIndexUR) == false){
			partialCLA=GetClaUpRight(nd, false);
			child=nd->right;
			if(child->IsInternal()){
				childCLA=GetClaDown(child, true);
				}
			blen1 = child->dlen;
			}
		else{//both of the UP clas must be dirty.  We'll use the down one as the
			//partial, and calc it now if necessary
			if(claMan->IsDirty(nd->claIndexDown) == true)
				partialCLA=GetClaDown(nd, true);
			else partialCLA=GetClaDown(nd, false);
			if(nd->anc!=NULL){
				child=nd->anc;
				if(child->left==nd){
					childCLA=GetClaUpLeft(child, true);
					}
				else if(child->right==nd){
					childCLA=GetClaUpRight(child, true);
					}
				else{
					//the node down that we want to get must be the root, and this
					//node must be it's middle des.  Remember that the cla for that
					//direction is stored as the root DOWN direction
					childCLA=GetClaDown(child);
					}
				blen1 = nd->dlen;
				}
			else{
				child=nd->left->next;
				if(child->IsInternal()){
					childCLA=GetClaDown(child, true);
					}
				blen1 = child->dlen;
				}
			}
		mod->CalcPmats(blen1, -1.0, Lprmat, Rprmat);

		if(fillFinalCLA==false){
			if(childCLA!=NULL){//if child is internal
				ProfScoreInt.Start();
				if(modSpec.IsNucleotide())
					lnL = GetScorePartialInternalRateHet(partialCLA, childCLA, &Lprmat[0]);
				else
					lnL = GetScorePartialInternalNState(partialCLA, childCLA, &Lprmat[0]);

				ProfScoreInt.Stop();
				}
			else{
				ProfScoreTerm.Start();
				if(modSpec.IsNucleotide())
					lnL = GetScorePartialTerminalRateHet(partialCLA, &Lprmat[0], child->tipData);
				else
					lnL = GetScorePartialTerminalNState(partialCLA, &Lprmat[0], child->tipData);

				ProfScoreTerm.Stop();
				}
			}

		else{
			//this is only for inferring internal states
			//careful!  This will have to be returned manually!!
			int wholeTreeIndex=claMan->AssignClaHolder();
			claMan->FillHolder(wholeTreeIndex, ROOT);
			claMan->ReserveCla(wholeTreeIndex);
			if(childCLA!=NULL)//if child is internal
				CalcFullCLAPartialInternalRateHet(claMan->GetCla(wholeTreeIndex), childCLA, &Lprmat[0], partialCLA);
			else
				CalcFullCLAPartialTerminalRateHet(claMan->GetCla(wholeTreeIndex), partialCLA, &Lprmat[0], child->tipData);

			return wholeTreeIndex;
			}
		}

	if(direction != ROOT){
		if(destCLA->rescaleRank >= rescaleEvery){
			ProfRescale.Start();
			if(modSpec.IsNucleotide())
				RescaleRateHet(destCLA);
			else
				RescaleRateHetNState(destCLA);

			ProfRescale.Stop();
			}
		}
	return -1;
	}

int Tree::Score(int rootNodeNum /*=0*/){

	TreeNode *rootNode=allNodes[rootNodeNum];

#ifdef EQUIV_CALCS
	if(dirtyEQ){
		ProfEQVectors.Start();
		root->SetEquivalentConditionalVectors(data);
		ProfEQVectors.Stop();
		dirtyEQ=false;
		}
#endif

	bool scoreOK=true;
	do{
		try{
			scoreOK=true;
				ConditionalLikelihoodRateHet( ROOT, rootNode);
			}
#if defined(NDEBUG)
			catch(int){
#else
			catch(int err){
#endif
				assert(err==1);
				scoreOK=false;
				MakeAllNodesDirty();
				rescaleEvery -= 2;
				ofstream resc("rescale.log", ios::app);
				resc << "rescale reduced to " << rescaleEvery << endl;
				resc.close();
				if(rescaleEvery<2) throw(ErrorException("Problem with rescaling during tree scoring.  Please report this error to zwickl@nescent.org."));
				}
		}while(scoreOK==false);

	return 1;
	}
/*
FLOAT_TYPE Tree::SubTreeScore( TreeNode *nd){
	//calculates the likelihood of the tree above the node passed in
	FLOAT_TYPE lnL = 0.0;
	int nSites = data->NChar();
	int ck;

	if(claMan->IsDirty(nd->claIndexDown)){
		if(mod->NRateCats()==1)
		ConditionalLikelihood( DOWN, nd);
		else
			ConditionalLikelihoodRateHet(DOWN, nd);
		}

	FLOAT_TYPE *cla=claMan->GetCla(nd->claIndexDown)->arr;
	int *underflow_mult=claMan->GetCla(nd->claIndexDown)->underflow_mult;

	// loop over all patterns
	long FLOAT_TYPE Lk;
	FLOAT_TYPE siteL;
	int ufcount=0;
	const int *countit=data->GetCounts();
	if(mod->PropInvar()==0.0){
		if(mod->NRateCats()==1){//no invariants or gamma
		for( int k = 0; k < nSites; k++ ){
				Lk =  mod->Pi(0) * cla[0] + mod->Pi(1) * cla[1] + mod->Pi(2) * cla[2] + mod->Pi(3) * cla[3];
				if(Lk<1e-300){
					printf("Underflow! site %d, multiplier %d\n", k, underflow_mult[k]);
					ufcount++;
					}
				cla+=4;
				siteL = (log( Lk ) - underflow_mult[k]);
				lnL += (  *countit++ *  siteL);
				}
			}
		else{//gamma, no invariants
			for( int k = 0; k < nSites; k++ ){
				Lk =  mod->Pi(0) * cla[0] + mod->Pi(1) * cla[1] + mod->Pi(2) * cla[2] + mod->Pi(3) * cla[3];
				Lk +=  mod->Pi(0) * cla[4] + mod->Pi(1) * cla[5] + mod->Pi(2) * cla[6] + mod->Pi(3) * cla[7];
				Lk +=  mod->Pi(0) * cla[8] + mod->Pi(1) * cla[9] + mod->Pi(2) * cla[10] + mod->Pi(3) * cla[11];
				Lk +=  mod->Pi(0) * cla[12] + mod->Pi(1) * cla[13] + mod->Pi(2) * cla[14] + mod->Pi(3) * cla[15];
				if(Lk<1e-300){
					printf("Underflow! site %d, multiplier %d\n", k, underflow_mult[k]);
					ufcount++;
					}
				cla+=16;
				//this is hard coded for 4 equal sized rate cats
				siteL = (log( Lk*.25 ) - underflow_mult[k]);
				lnL += (  *countit * siteL);
				countit++;
				}
			}
		}
	else {
		FLOAT_TYPE prI=mod->PropInvar();
		int lastConst=data->LastConstant();
		const int *conBases=data->GetConstBases();

		if(mod->NRateCats()==1){//invariants without gamma
	for( int k = 0; k < nSites; k++ ){
		assert(0);
		//this isn't valid :mod->Pi(conBases[k]), because the con bases are coded as 1 2 4 8 for amiguity
		Lk =  mod->Pi(0) * cla[0] + mod->Pi(1) * cla[1] + mod->Pi(2) * cla[2] + mod->Pi(3) * cla[3];
		if(Lk<1e-300){
			printf("Underflow! site %d, multiplier %d\n", k, underflow_mult[k]);
			ufcount++;
			}
		cla+=4;
				if(k > lastConst){
					siteL = log( Lk * (1.0-prI)) - underflow_mult[k];
					lnL += ( *countit++ * siteL);
					}
				else{
					siteL = log( Lk * (1.0-prI) + (prI * mod->Pi(conBases[k])) * exp((FLOAT_TYPE)underflow_mult[k]));
					lnL += ( *countit++ * (siteL + underflow_mult[k]));
					}
				}
			}
		else{//gamma and invariants
			FLOAT_TYPE scaledGammaProp=0.25 * (1.0-prI);
			assert(0);
			//this isn't valid :mod->Pi(conBases[k]), because the con bases are coded as 1 2 4 8 for amiguity
			for( int k = 0; k < nSites; k++ ){
				Lk =  mod->Pi(0) * cla[0] + mod->Pi(1) * cla[1] + mod->Pi(2) * cla[2] + mod->Pi(3) * cla[3];
				Lk +=  mod->Pi(0) * cla[4] + mod->Pi(1) * cla[5] + mod->Pi(2) * cla[6] + mod->Pi(3) * cla[7];
				Lk +=  mod->Pi(0) * cla[8] + mod->Pi(1) * cla[9] + mod->Pi(2) * cla[10] + mod->Pi(3) * cla[11];
				Lk +=  mod->Pi(0) * cla[12] + mod->Pi(1) * cla[13] + mod->Pi(2) * cla[14] + mod->Pi(3) * cla[15];
				if(Lk<1e-300){
					printf("Underflow! site %d, multiplier %d\n", k, underflow_mult[k]);
					ufcount++;
					}
				cla+=16;
				if(k > lastConst){
					siteL = log( Lk * scaledGammaProp) - underflow_mult[k];
					lnL += ( *countit++ * siteL);
					}
				else{
					siteL = log( Lk * scaledGammaProp + (prI * mod->Pi(conBases[k])) * exp((FLOAT_TYPE)underflow_mult[k]));
					lnL += ( *countit++ * (siteL + underflow_mult[k]));
					}
				}
			}
		}
	return lnL;
	}
*/
/*
FLOAT_TYPE Tree::SubTreeScoreRateHet( TreeNode *nd){
	//calculates the likelihood of the tree above the node passed in
	FLOAT_TYPE sublnL = 0.0;
	int nSites = data->NChar();
	int ck;

	if(claMan->IsDirty(nd->claIndexDown))
		ConditionalLikelihoodRateHet(DOWN, nd);


	FLOAT_TYPE *cla=claMan->GetCla(nd->claIndexDown)->arr;
	int *underflow_mult=claMan->GetCla(nd->claIndexDown)->underflow_mult;

	// loop over all patterns
	long FLOAT_TYPE Lk;
	int ufcount=0;
	const int *countit=data->GetCounts();
	for( int k = 0; k < nSites; k++ ){
		Lk =  mod->Pi(0) * cla[0] + mod->Pi(1) * cla[1] + mod->Pi(2) * cla[2] + mod->Pi(3) * cla[3];
		Lk +=  mod->Pi(0) * cla[4] + mod->Pi(1) * cla[5] + mod->Pi(2) * cla[6] + mod->Pi(3) * cla[7];
		Lk +=  mod->Pi(0) * cla[8] + mod->Pi(1) * cla[9] + mod->Pi(2) * cla[10] + mod->Pi(3) * cla[11];
		Lk +=  mod->Pi(0) * cla[12] + mod->Pi(1) * cla[13] + mod->Pi(2) * cla[14] + mod->Pi(3) * cla[15];
		if(Lk<1e-300){
			printf("Underflow! site %d, multiplier %d\n", k, underflow_mult[k]);
			ufcount++;
			}
		cla+=16;

		sublnL += (  *countit * (log( Lk*.25 ) - underflow_mult[k]) );
		countit++;
		}
	return sublnL;
}
*/
void Tree::TraceDirtynessToRoot(TreeNode *nd){
	SweepDirtynessOverTree(nd);

/*
	while(nd){
		if(nd->nodeNum==0 || nd->nodeNum>numTipsTotal) nd->claIndexDown=claMan->SetDirty(nd->claIndexDown, true);
		nd=nd->anc;
		}

*/	}

void Tree::SweepDirtynessOverTree(TreeNode *nd, TreeNode *from/*=NULL*/){
	lnL=-1;

	//this will be the case if we are simply making the tree structure but
	//never intend to score it
	if(nd->IsInternal() && nd->claIndexDown == -1){
		return;
		}

	if(from==NULL){
		//if this is the branch where the dirtyness starts
		if(nd->IsInternal()){
			nd->claIndexUL=claMan->SetDirty(nd->claIndexUL);
			nd->claIndexUR=claMan->SetDirty(nd->claIndexUR);
			if(nd->left->IsInternal()) SweepDirtynessOverTree(nd->left, nd);
			if(nd->right->IsInternal()) SweepDirtynessOverTree(nd->right, nd);
			}
		if(nd->anc!=NULL) SweepDirtynessOverTree(nd->anc, nd);
		}
	else{
	//if the change was below, invalidating clas above, also if the change
	//was on the path connecting to the central des of the root
		if(from==nd->anc || (nd->IsRoot() && from==nd->left->next)){
			nd->claIndexUL=claMan->SetDirty(nd->claIndexUL);
			nd->claIndexUR=claMan->SetDirty(nd->claIndexUR);
			if(nd->left->IsInternal()) SweepDirtynessOverTree(nd->left, nd);
			if(nd->right->IsInternal()) SweepDirtynessOverTree(nd->right, nd);
			}
		else if(from==nd->left){
			nd->claIndexUR=claMan->SetDirty(nd->claIndexUR);
			nd->claIndexDown=claMan->SetDirty(nd->claIndexDown);
			if(nd->right->IsInternal()) SweepDirtynessOverTree(nd->right, nd);
			if(nd->anc!=NULL) SweepDirtynessOverTree(nd->anc, nd);
			else if(nd->left->next->IsInternal()) SweepDirtynessOverTree(nd->left->next, nd);
			}
		else if(from==nd->right){
			nd->claIndexUL=claMan->SetDirty(nd->claIndexUL);
			nd->claIndexDown=claMan->SetDirty(nd->claIndexDown);
			if(nd->left->IsInternal()) SweepDirtynessOverTree(nd->left, nd);
			if(nd->anc!=NULL) SweepDirtynessOverTree(nd->anc, nd);
			else if(nd->left->next->IsInternal()) SweepDirtynessOverTree(nd->left->next, nd);
			}
		}
	}

void Tree::TraceDirtynessToNode(TreeNode *nd, int tonode){
	if(nd->nodeNum==0 || nd->nodeNum>numTipsTotal) nd->claIndexDown=claMan->SetDirty(nd->claIndexDown);
	while(nd->nodeNum!=tonode){
		nd=nd->anc;
		if(nd->nodeNum==0 || nd->nodeNum>numTipsTotal) nd->claIndexDown=claMan->SetDirty(nd->claIndexDown);
		}
	}

void Tree::SortAllNodesArray(){
	//this function will simply sort the nodes in the allNodes **TreeNode array by their nodeNum
	//having the nodes always in order will make some other operations much simpler
	//the root(nodenum=0) and terminals(nodenums=1->Ntax) should already be in order, so just sort
	//starting at Ntax+1.  I'm making up a kind of wacky algorithm for this. DZ 10-30-02
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		if(allNodes[i]->nodeNum!=i){
			while(allNodes[i]->nodeNum!=i){
				TreeNode *toPlace=allNodes[i];
				int rightPlace=toPlace->nodeNum;
				TreeNode *temp=allNodes[rightPlace];//copy the node that is in toPlace's rightful place
				allNodes[rightPlace]=allNodes[i];   //put toPlace where it belongs
				allNodes[i]=temp;					//put the node that was moved in allNodes[i];
				}
			}
		}
	}

void Tree::EliminateNode(int nn){
	//DZ 10-30-02 this function will permenantly get rid of a node and correct all of the other nodeNums so that
	//there isn't a hole in the middle.  I think this just needs to be called when an inital tree is made trifrcating
	//at the root.  This isn't the prettiest thing, but I can't think of an obvious way to make a tree that has 3 des
	//from the root in the first place
	delete allNodes[nn];
	for(int i=nn;i<allNodes.size()-1;i++){
		allNodes[i]=allNodes[i+1];
		allNodes[i]->nodeNum=i;
		}
	std::vector<TreeNode *>::iterator it = allNodes.begin();
	it += nn;
	allNodes.erase(it);
	}

//CAREFUL!  This is called from CheckBalance and assumes that this tree
//does not share CLAs with any other.
void Tree::RotateNodesAtRoot(TreeNode *newroot){
	//DZ 11-3-02 This can be used to rebalance the tree
	//I'm assuming that this will be called with one of the des of the root;
	assert(newroot->anc==root);
	assert(newroot->IsInternal());
	//detach the newroot from root, making it bifurcating
	if(newroot==root->left){
		root->left=newroot->next;
		root->left->prev=NULL;
		//DEBUG
		int temp = root->claIndexDown;
		root->claIndexDown = root->claIndexUL;
		root->claIndexUL = temp;
		}
	else if(newroot==root->left->next){
		root->left->next=root->right;
		root->right->prev=root->left;
		}
	else{
		root->right=root->left->next;
		root->right->next=NULL;
		//DEBUG
		int temp = root->claIndexDown;
		root->claIndexDown = root->claIndexUR;
		root->claIndexUR = temp;
		}
	//now make the root the middle des of newroot and correct the dlens
	root->anc=newroot;
	newroot->left->next=root;
	root->prev=newroot->left;
	root->next=newroot->right;
	newroot->right->prev=root;
	root->dlen=newroot->dlen;
	newroot->dlen=-1;
	newroot->anc=NULL;
	newroot->next=newroot->prev=NULL;
	 //now make the new root nodeNum 0 in the allNodes array
	TreeNode *tempnode=root;

	//DEBUG
/*	int tempindexdown=root->claIndexDown;
	root->claIndexDown=newroot->claIndexDown;
	newroot->claIndexDown=tempindexdown;
	int tempindexUL=root->claIndexUL;
	root->claIndexUL=newroot->claIndexUL;
	newroot->claIndexUL=tempindexUL;
	int tempindexUR=root->claIndexUR;
	root->claIndexUR=newroot->claIndexUR;
	newroot->claIndexUR=tempindexUR;
*/	root=newroot;
	allNodes[0]=newroot;
	tempnode->nodeNum=root->nodeNum;
	root->nodeNum=0;
	allNodes[tempnode->nodeNum]=tempnode;
	bipartCond = DIRTY;
	//this form of setdirty won't shift every copy to a new topo, but will set them to dirty
//	claMan->SetDirtyButDoNotMove(0, root->claIndex);
//	claMan->SetDirtyButDoNotMove(tempnode->nodeNum, tempnode->claIndex);
//	root->claIndexDown=claMan->SetDirty(root->claIndexDown);
//	tempnode->claIndexDown=claMan->SetDirty(tempnode->claIndexDown);
	}

//CAREFUL here!  This function assumes that this tree and ONLY this tree
//points to a set of CLAs.  The indeces should all be valid on exit
//but strange things may happen if other trees also point to them.
void Tree::CheckBalance(){
	//evaluate the average depth of all branches in the tree
	int lb=0, mb=0, rb=0;
	int ls=0, ms=0, rs=0;
	int llb=0, lrb=0, mlb=0, mrb=0, rlb=0, rrb=0;
	int lls=0, lrs=0, mls=0, mrs=0, rls=0, rrs=0;
	int lastRot=0;
	if(root->left->IsInternal()){
		root->left->left->CountSubtreeBranchesAndDepth(llb, lls, 3, true);
		root->left->right->CountSubtreeBranchesAndDepth(lrb, lrs, 3, true);
		lb=llb+lrb+2;
		ls=lls+lrs+4;
		}
	if(root->left->next->IsInternal()){
		root->left->next->left->CountSubtreeBranchesAndDepth(mlb, mls, 3, true);
		root->left->next->right->CountSubtreeBranchesAndDepth(mrb, mrs, 3, true);
		mb=mlb+mrb+2;
		ms=mls+mrs+4;
		}
	if(root->right->IsInternal()){
		root->right->left->CountSubtreeBranchesAndDepth(rlb, rls, 3, true);
		root->right->right->CountSubtreeBranchesAndDepth(rrb, rrs, 3, true);
		rb=rlb+rrb+2;
		rs=rls+rrs+4;
		}
/*
	int dl=0, dm=0, dr=0;
	root->left->CalcDepth(dl);
	root->left->next->CalcDepth(dm);
	root->right->CalcDepth(dr);
*/
	do{
		int cur=ls+ms+rs+3;
		int rotLeft=(lls-llb+lrs-lrb+2+ms+mb+rs+rb+5);
		int rotMid=(mls-mlb+mrs-mrb+2+ls+lb+rs+rb+5);
		int rotRight=(rls-rlb+rrs-rrb+2+ms+mb+ls+lb+5);

		if(cur<=rotLeft&&cur<=rotMid&&cur<=rotRight) return;
		else if(rotLeft<rotMid&&rotLeft<rotRight){
			RotateNodesAtRoot(root->left);
			lastRot=1;
			}
		else if(rotMid<rotLeft&&rotMid<rotRight){
			RotateNodesAtRoot(root->left->next);
			lastRot=2;
			}
		else if(rotRight<cur){
			RotateNodesAtRoot(root->right);
			lastRot=3;
			}

		lb=mb=rb=ls=ms=rs=llb=lrb=mlb=mrb=rlb=rrb=lls=lrs=mls=mrs=rls=rrs=0;

		if(root->left->IsInternal()){
			root->left->left->CountSubtreeBranchesAndDepth(llb, lls, 3, true);
			root->left->right->CountSubtreeBranchesAndDepth(lrb, lrs, 3, true);
			lb=llb+lrb+2;
			ls=lls+lrs+4;
			}
		if(root->left->next->IsInternal()){
			root->left->next->left->CountSubtreeBranchesAndDepth(mlb, mls, 3, true);
			root->left->next->right->CountSubtreeBranchesAndDepth(mrb, mrs, 3, true);
			mb=mlb+mrb+2;
			ms=mls+mrs+4;
			}
		if(root->right->IsInternal()){
			root->right->left->CountSubtreeBranchesAndDepth(rlb, rls, 3, true);
			root->right->right->CountSubtreeBranchesAndDepth(rrb, rrs, 3, true);
			rb=rlb+rrb+2;
			rs=rls+rrs+4;
			}
/*
		root->left->CalcDepth(dl);
		root->left->next->CalcDepth(dm);
		root->right->CalcDepth(dr);



*/		}while(1);
	}

void Tree::SwapAndFreeNodes(const TreeNode *cop){
	assert(cop->left);//only swap internal nodes
	int tofree=cop->nodeNum;
	//we need to actually swap the memory addresses of the nodes in the allnodes array so that all other node pointers in the
	//tree stay correct
	if(allNodes[tofree]->IsAttached()){
		//find a node to swap with
		int unused=FindUnusedNode(numTipsTotal+1);
		TreeNode *tempnode=allNodes[unused];
		//swap the adresses of the nodes
		allNodes[unused]=allNodes[tofree];
		allNodes[tofree]=tempnode;
		//now adjust the nodeNums and claIndeces
		int temp=allNodes[unused]->nodeNum;
		allNodes[unused]->nodeNum=allNodes[tofree]->nodeNum;
		allNodes[tofree]->nodeNum=temp;

		MakeNodeDirty(allNodes[unused]);
		MakeNodeDirty(allNodes[tofree]);
		/*
		temp=allNodes[unused]->claIndexDown;
		allNodes[unused]->claIndexDown=allNodes[tofree]->claIndexDown;
		allNodes[tofree]->claIndexDown=temp;
		temp=allNodes[unused]->claIndexUL;
		allNodes[unused]->claIndexUL=allNodes[tofree]->claIndexUL;
		allNodes[tofree]->claIndexUL=temp;
		temp=allNodes[unused]->claIndexUR;
		allNodes[unused]->claIndexUR=allNodes[tofree]->claIndexUR;
		allNodes[tofree]->claIndexUR=temp;
		*/
		//set the nodes to dirty
//		assert(0);
//		allNodes[tofree]->claIndex=claMan->SetDirty(allNodes[tofree]->nodeNum, allNodes[tofree]->claIndex, true);
//		allNodes[unused]->claIndex=claMan->SetDirty(allNodes[unused]->nodeNum, allNodes[unused]->claIndex, true);
		allNodes[unused]->SetAttached(true);
		allNodes[tofree]->SetAttached(true);//actual its not attached, but we need to mark it as such so it isn't used as a connector
		}
	else//this is odd, but if a node will need to be used to
		//mimic nodenums in the subtree, but was already unattached,
		//we need to mark it as attached so that it isn't used for
		//some other purpose.
		allNodes[tofree]->SetAttached(true);

	if(cop->left->left) SwapAndFreeNodes(cop->left);
	if(cop->right->left) SwapAndFreeNodes(cop->right);
	}

void Tree::CalcBipartitions(bool standardize) const {
	if(!(bipartCond == CLEAN_STANDARDIZED && standardize == true) &&
		!(bipartCond == CLEAN_UNSTANDARDIZED && standardize == false)){

		if(bipartCond == CLEAN_UNSTANDARDIZED && standardize == true)
			root->StandardizeBipartition();
		else
			root->CalcBipartition(standardize);
		if(standardize)	bipartCond = CLEAN_STANDARDIZED;
		else bipartCond = CLEAN_UNSTANDARDIZED;
		}
	}

void Tree::OutputBipartitions(){
	ofstream out("biparts.log", ios::app);
	root->OutputBipartition(out);
	}
/*
void Tree::SetDistanceBasedBranchLengthsAroundNode(TreeNode *nd){
	FLOAT_TYPE D1, D2, D3, k1, k2, k3, k4, a, b, c;
	TreeNode *T1, *T2, *T3, *T4;

	FindNearestTerminalUp(nd->left, T1, k1);
	FindNearestTerminalUp(nd->right, T2, k2);
	FindNearestTerminalsDown(nd->anc, nd, T3, T4, k3, k4);
//	FindNearestTerminalUp(nd->, T2, k2);

	if(k4<k3){
		T3=T4;
		k3=k4;
		}
#ifdef FLEX_RATES
	assert(0);
#else
	D1=CalculatePDistance(T1->tipData, T2->tipData, data->NChar())/(1.0-mod->PropInvar()) - k1 -k2;
	D2=CalculatePDistance(T1->tipData, T3->tipData, data->NChar())/(1.0-mod->PropInvar()) - k1 -k3;
	D3=CalculatePDistance(T2->tipData, T3->tipData, data->NChar())/(1.0-mod->PropInvar()) - k2 -k3;
#endif
	b=(D3-D2+D1)*0.5;
	if(b < min_brlen) b=min_brlen;
	a=D1-b;
	if(a < min_brlen) a=min_brlen;
	c=D2-a;
	if(c < min_brlen) c=min_brlen;

	nd->left->dlen=a;
	nd->right->dlen=b;
	nd->dlen=c;

	SweepDirtynessOverTree(nd->left);
	SweepDirtynessOverTree(nd);
	SweepDirtynessOverTree(nd->right);
	}

void Tree::FindNearestTerminalUp(TreeNode *start, TreeNode *&term, FLOAT_TYPE &dist){
	dist=999999.9;
	int nodeDist=9999;
	sprRange.clear();
	sprRange.setseed(start->nodeNum);
	int range=10;
    for(int i = 0;i<range;i++){
      int j =  sprRange.total;
		for(int k=0; k < j; k++){
			if(sprRange.front[k]==i){
				TreeNode *cur=allNodes[sprRange.element[k]];
				if(cur->left!=NULL){
				    sprRange.addelement(cur->left->nodeNum, i+1, sprRange.pathlength[k]+cur->left->dlen);
				    sprRange.addelement(cur->right->nodeNum, i+1, sprRange.pathlength[k]+cur->right->dlen);
				    }
				else{
					//if(sprRange.pathlength[k]<dist){
					if(sprRange.front[k]<nodeDist){
						nodeDist=sprRange.front[k];
						term=cur;
						dist=sprRange.pathlength[k];
						}
					}
				}
		    }
		}
	}

void Tree::FindNearestTerminalsDown(TreeNode *start, TreeNode *from, TreeNode *&term1, TreeNode *&term2, FLOAT_TYPE &dist1, FLOAT_TYPE &dist2){
	dist1=dist2=999999.9;
	int nodeDist1=9999, nodeDist2=9999;
	sprRange.clear();
	if(from==start->left) sprRange.setseed(start->right->nodeNum, start->right->dlen);
	else sprRange.setseed(start->left->nodeNum, start->left->dlen);
	int range=10;
    for(int i = 0;i<range;i++){
      int j =  sprRange.total;
		for(int k=0; k < j; k++){
			if(sprRange.front[k]==i){
				TreeNode *cur=allNodes[sprRange.element[k]];
				if(cur->left!=NULL){
				    sprRange.addelement(cur->left->nodeNum, i+1, sprRange.pathlength[k]+cur->left->dlen);
				    sprRange.addelement(cur->right->nodeNum, i+1, sprRange.pathlength[k]+cur->right->dlen);
				    }
				else{
					//if(sprRange.pathlength[k]<dist1){
					if(sprRange.front[k]<nodeDist1){
						nodeDist1=sprRange.front[k];
						term1=cur;
						dist1=sprRange.pathlength[k];
						}
					}
				}
		    }
		}

	sprRange.clear();
	if(start->anc != NULL){
		sprRange.setseed(start->anc->nodeNum, start->dlen);
		for(int i = 0;i<range;i++){
	      int j =  sprRange.total;
			for(int k=0; k < j; k++){
				if(sprRange.front[k]==i){
					TreeNode *cur=allNodes[sprRange.element[k]];
					if(cur->left!=NULL){
					    if(cur->left!=from->anc) sprRange.addelement(cur->left->nodeNum, i+1, sprRange.pathlength[k]+cur->left->dlen);
					    if(cur->right!=from->anc) sprRange.addelement(cur->right->nodeNum, i+1, sprRange.pathlength[k]+cur->right->dlen);
					    }
					else{
						//if(sprRange.pathlength[k]<dist2){
						if(sprRange.front[k]<nodeDist2){
							nodeDist2=sprRange.front[k];
							term2=cur;
							dist2=sprRange.pathlength[k];
							}
						}
					if(cur->anc) sprRange.addelement(cur->anc->nodeNum, i+1, sprRange.pathlength[k]+cur->dlen);
					else sprRange.addelement(cur->left->next->nodeNum, i+1, sprRange.pathlength[k]+cur->left->next->dlen);
					}
			    }
			}
		}
	else{
		if(from!=start->left->next) sprRange.setseed(start->left->next->nodeNum, start->left->next->dlen);
		else sprRange.setseed(start->right->nodeNum, start->right->dlen);
		int range=10;
	    for(int i = 0;i<range;i++){
	      int j =  sprRange.total;
			for(int k=0; k < j; k++){
				if(sprRange.front[k]==i){
					TreeNode *cur=allNodes[sprRange.element[k]];
					if(cur->left!=NULL){
					    sprRange.addelement(cur->left->nodeNum, i+1, sprRange.pathlength[k]+cur->left->dlen);
					    sprRange.addelement(cur->right->nodeNum, i+1, sprRange.pathlength[k]+cur->right->dlen);
					    }
					else{
						//if(sprRange.pathlength[k]<dist2){
						if(sprRange.front[k]<nodeDist2){
							nodeDist2=sprRange.front[k];
							term2=cur;
							dist2=sprRange.pathlength[k];
							}
						}
					}
			    }
			}
		}
	assert(term1 != term2);
	}
*/
void Tree::OptimizeBranchesAroundNode(TreeNode *nd, FLOAT_TYPE optPrecision, int subtreeNode){
	//depricated
	assert(0);
	//this function will optimize the three branches (2 descendents and one anc) connecting
	//to it.  It assumes that everything that is dirty has been marked so.
	//by default there is only a single optimization pass over the three nodes
/*	FLOAT_TYPE precision1, precision2;

	if(subtreeNode==0) SetAllTempClasDirty();

	precision1=optPrecision;// * 0.5;
	if(optPrecision > .2) precision2=0.0;
	else precision2=precision1 * 0.5;

	if(nd != root){
		BrentOptimizeBranchLength(precision1, nd, false);
		BrentOptimizeBranchLength(precision1, nd->left, false);
		BrentOptimizeBranchLength(precision1, nd->right, false);
		}
	else{
		BrentOptimizeBranchLength(precision1, nd->left, false);
		BrentOptimizeBranchLength(precision1, nd->left->next, false);
		BrentOptimizeBranchLength(precision1, nd->right, false);
		}
*/
/*
	if(precision2 > 0){
		//if were're doing multiple optimization passes, only this stuff needs to be set dirty
		claMan->SetDirty(nd->nodeNum, nd->claIndex, true);
		claMan->SetTempDirty(nd->nodeNum, true);
		if(nd != root) claMan->SetTempDirty(nd->anc->nodeNum, true);

		if(nd != root){
			BrentOptimizeBranchLength(precision2, nd, false);
			BrentOptimizeBranchLength(precision2, nd->left, false);
			BrentOptimizeBranchLength(precision2, nd->right, false);
			}
		else {
			BrentOptimizeBranchLength(precision2, nd->left, false);
			BrentOptimizeBranchLength(precision2, nd->left->next, false);
			BrentOptimizeBranchLength(precision2, nd->right, false);
			}
		}
*/
/*	//these must be called after all optimization passes are done around this node
	TraceDirtynessToRoot(nd);
	if(subtreeNode==0)
		SetAllTempClasDirty();
	else SetTempClasDirtyWithinSubtree(subtreeNode);
*/	}

void Tree::RerootHere(int newroot){
	//DJZ 1-5-05 adding functionality to adjust the direction of existing clas
	//so that they are still valid in the new context, rather than just dirtying everything
	//DJZ 11/19/07 removing CLA adjustment code because it was buggy and didn't check the
	//number of individuals that pointed to the same CLA, and so sometimes screwed things up.
	//REMEMBER that the mutation_type of the individual this is called for needs to be
	// "|= rerooted" so that the topo numbers are updated properly

	TreeNode *nroot=allNodes[newroot];

	TreeNode *prevnode=nroot;
	TreeNode *curnode=nroot->anc;
	TreeNode *nextnode=nroot->anc->anc;

	//this is necessary to properly dirty clas
	TreeNode *lastOnPath=nroot;
	while(lastOnPath->anc != root) lastOnPath = lastOnPath->anc;
	SweepDirtynessOverTree(lastOnPath);

	//first trace down to the old root and fix all the blens
	//Each branch with take the length of its descendent on that path
	//this will be easiest recursively
	nroot->FlipBlensToRoot(0);
	SweepDirtynessOverTree(nroot);

	//now take the new root's current ancestor and make it the middle des
	//note that the existing cla directions at this node are still valid
	nroot->left->next=curnode;
	curnode->next=nroot->right;
	nroot->right->prev=curnode;
	curnode->prev=nroot->left;

	//this needs to work slightly differently if the old root is the anc of the new one
	if(curnode!=root){
		if(prevnode==curnode->left){
			curnode->left=curnode->anc;
			//curnode->AdjustClasForReroot(UPLEFT);
			}
		else{
			curnode->right=curnode->anc;
			//curnode->AdjustClasForReroot(UPRIGHT);
			}
//		SweepDirtynessOverTree(curnode);

		curnode->left->next=curnode->right;
		curnode->left->prev=NULL;
		curnode->right->prev=curnode->left;
		curnode->right->next=NULL;

		prevnode=curnode;
		curnode=nextnode;
		nextnode=nextnode->anc;
		}

	curnode->anc=prevnode;
	nroot->anc=NULL;

	while(curnode!=root){
		if(prevnode==curnode->left){
			curnode->left=nextnode;
			//curnode->AdjustClasForReroot(UPLEFT);
			}
		else{
			curnode->right=nextnode;
			//curnode->AdjustClasForReroot(UPRIGHT);
			}
//		SweepDirtynessOverTree(curnode);

		curnode->left->next=curnode->right;
		curnode->left->prev=NULL;
		curnode->right->prev=curnode->left;
		curnode->right->next=NULL;

		curnode->anc=prevnode;

		prevnode=curnode;
		curnode=nextnode;
		nextnode=nextnode->anc;
		}

	//now deal with the old root, which is now curnode
	if(prevnode==curnode->left){
		curnode->left=curnode->right->prev;
		curnode->left->prev=NULL;
		//curnode->AdjustClasForReroot(UPLEFT);
		}
	else if(prevnode==curnode->left->next){
		curnode->left->next=curnode->right;
		curnode->right->prev=curnode->left;
		//clas don't need to be adjusted in this case
		}
	else{
		curnode->right=curnode->left->next;
		curnode->right->next=NULL;
		//curnode->AdjustClasForReroot(UPRIGHT);
		}
	MakeNodeDirty(curnode);

	curnode->anc=prevnode;

	//now we just need to make the newroot node0 and swap it with the old root, which means moving the
	//_data_ to node 0, not just swapping the memory addresses

	SwapNodeDataForReroot(nroot);

	root->CheckTreeFormation();
	bipartCond = DIRTY;
//	MakeAllNodesDirty();
//	Score();
	}

void Tree::SwapNodeDataForReroot(TreeNode *nroot){
	TreeNode tempold;
	tempold.left=root->left;
	tempold.right=root->right;
	tempold.next=root->next;
	tempold.prev=root->prev;
	//note that we need to watch out here if the new root is currently the anc of the old root
	if(root->anc==nroot) tempold.anc=root;
	else tempold.anc=root->anc;
	tempold.dlen=root->dlen;

	tempold.claIndexDown=root->claIndexDown;
	tempold.claIndexUL=root->claIndexUL;
	tempold.claIndexUR=root->claIndexUR;

	TreeNode tempnew;
	tempnew.left=nroot->left;
	tempnew.right=nroot->right;
	tempnew.next=nroot->next;
	tempnew.prev=nroot->prev;
	tempnew.anc=nroot->anc;
	tempnew.dlen=nroot->dlen;
	tempnew.claIndexDown=nroot->claIndexDown;
	tempnew.claIndexUL=nroot->claIndexUL;
	tempnew.claIndexUR=nroot->claIndexUR;

	root->left=tempnew.left;
	root->left->anc=root;
	root->right=tempnew.right;
	root->right->anc=root;
	root->left->next->anc=root;
	root->prev=root->next=NULL;
	root->anc=NULL;
	root->dlen=-1;
	root->claIndexDown=tempnew.claIndexDown;
	root->claIndexUL=tempnew.claIndexUL;
	root->claIndexUR=tempnew.claIndexUR;

	MakeNodeDirty(root);

	nroot->left=tempold.left;
	nroot->left->anc=nroot;
	nroot->right=tempold.right;
	nroot->next=tempold.next;
	if(nroot->next) nroot->next->prev=nroot;
	nroot->prev=tempold.prev;
	if(nroot->prev) nroot->prev->next=nroot;
	nroot->right->anc=nroot;
	nroot->anc=tempold.anc;
	nroot->claIndexDown=tempold.claIndexDown;
	nroot->claIndexUL=tempold.claIndexUL;
	nroot->claIndexUR=tempold.claIndexUR;

	MakeNodeDirty(nroot);

	if(nroot->anc->left==root){
		nroot->anc->left=nroot;
		nroot->prev=NULL;
		nroot->next=nroot->anc->right;
		nroot->next->prev=nroot;
		}
	else if(nroot->anc->right==root){
		nroot->anc->right=nroot;
		nroot->next=NULL;
		nroot->prev=nroot->anc->left;
		nroot->prev->next=nroot;
		}
	else{
		nroot->anc->left->next=nroot;
//		nroot->next=NULL;
		nroot->prev=nroot->anc->left;
//		nroot->prev->next=nroot;
		}
	nroot->dlen=tempold.dlen;
	}


void Tree::MakeNodeDirty(TreeNode *nd){
	if(nd->claIndexDown != -1)
		nd->claIndexDown=claMan->SetDirty(nd->claIndexDown);
	if(nd->claIndexUL != -1)
		nd->claIndexUL=claMan->SetDirty(nd->claIndexUL);
	if(nd->claIndexUR != -1)
		nd->claIndexUR=claMan->SetDirty(nd->claIndexUR);
	}

void Tree::RemoveTempClaReservations(){
	if(memLevel > 1){
		for(int i=numTipsTotal+1;i<allNodes.size();i++){
			claMan->ClearTempReservation(allNodes[i]->claIndexDown);
			}
		}

	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		claMan->ClearTempReservation(allNodes[i]->claIndexUR);
		}
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		claMan->ClearTempReservation(allNodes[i]->claIndexUL);
		}
	}

void Tree::ReclaimUniqueClas(){
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		if(claMan->GetNumAssigned(allNodes[i]->claIndexDown) == 1){
			claMan->ReclaimSingleCla(allNodes[i]->claIndexDown);
			}
		if(claMan->GetNumAssigned(allNodes[i]->claIndexUL) == 1){
			claMan->ReclaimSingleCla(allNodes[i]->claIndexUL);
			}
		if(claMan->GetNumAssigned(allNodes[i]->claIndexUR) == 1){
			claMan->ReclaimSingleCla(allNodes[i]->claIndexUR);
			}
		}
	}

void Tree::MarkUpwardClasToReclaim(int subtreeNode){
	//if we are somewhat low on clas, mark some reclaimable that were
	//used tracing the likelihood upward for blen optimization
	assert(0);
	if(subtreeNode==0){
/*		if(memLevel==2){
			if(allNodes[0]->claIndexUL > 0)
				claMan->MarkReclaimable(allNodes[0]->claIndexUL, 2);
			if(allNodes[0]->claIndexUR > 0)
				claMan->MarkReclaimable(allNodes[0]->claIndexUR, 2);
			}
*/		for(int i=numTipsTotal+1;i<allNodes.size();i++){
//			claMan->MarkReclaimable(allNodes[i]->claIndexUL, 2, false);
//			claMan->MarkReclaimable(allNodes[i]->claIndexUR, 2, false);
			}
		}
	else{
		for(int i=numTipsTotal+1;i<allNodes.size();i++){
			if((allNodes[i]->nodeNum != subtreeNode) && (allNodes[i]->nodeNum != allNodes[subtreeNode]->anc->nodeNum)){
				if(allNodes[i]->claIndexUL > 0){
//					claMan->MarkReclaimable(allNodes[i]->claIndexUL, 2, false);
					}
				if(allNodes[i]->claIndexUR > 0){
//					claMan->MarkReclaimable(allNodes[i]->claIndexUR, 2, false);
					}
				}
			}
		}
	}

void Tree::MarkDownwardClasToReclaim(int subtreeNode){
	//if we're calling this, we must really be desperate for clas
	//this should only be called after the tree has been scored
	assert(0);

	if(subtreeNode==0){
		if(memLevel<3){
			for(int i=numTipsTotal+1;i<allNodes.size();i++){
//				claMan->MarkReclaimable(allNodes[i]->claIndexDown, 1);
				}
			}
		else{
			for(int i=numTipsTotal+1;i<allNodes.size();i++){
//				claMan->MarkReclaimable(allNodes[i]->claIndexDown, 1, false);
				}
			}
		}
	else{
		return;  //I think that this is safe, since in general many fewer node will be necessary in subtree mode
		for(int i=numTipsTotal+1;i<allNodes.size();i++){
			if((allNodes[i]->nodeNum != subtreeNode) && (allNodes[i]->nodeNum != allNodes[subtreeNode]->anc->nodeNum)){
				if(allNodes[i]->claIndexUL > 0){
//					claMan->MarkReclaimable(allNodes[i]->claIndexUL, 1);
					}
				}
			}
		}
	}

void Tree::MarkClasNearTipsToReclaim(int subtreeNode){
	assert(0);
	if(subtreeNode==0){
		for(int i=1;i<numTipsTotal;i++){
//			claMan->MarkReclaimable(allNodes[i]->anc->claIndexDown, 1, false);
			}
		}
	else{
		return;  //I think that this is safe, since in general many fewer node will be necessary in subtree mode
		for(int i=numTipsTotal+1;i<allNodes.size();i++){
			if((allNodes[i]->nodeNum != subtreeNode) && (allNodes[i]->nodeNum != allNodes[subtreeNode]->anc->nodeNum)){
				if(allNodes[i]->claIndexUL > 0){
//					claMan->MarkReclaimable(allNodes[i]->claIndexUL, 1);
					}
				}
			}
		}
	}

void Tree::OutputNthClaAcrossTree(ofstream &deb, TreeNode *nd, int site){
	//int site=0;
	int nstates = mod->NStates();
	int index=nstates * mod->NRateCats() * site;

	//this version outputs the indeces even for dirty clas
	if(nd->IsInternal()){
		deb << nd->nodeNum << "\t0\t" << nd->claIndexDown << "\t";
		if(claMan->IsDirty(nd->claIndexDown) == false){
			for(int i=0;i<nstates*mod->NRateCats();i++) deb << claMan->GetCla(nd->claIndexDown)->arr[index+i] << "\t";
			deb << claMan->GetCla(nd->claIndexDown)->underflow_mult[site];
			}
		deb << "\n";
		}
	if(nd->IsInternal()){
		deb << nd->nodeNum << "\t1\t" << nd->claIndexUL << "\t";
		if(claMan->IsDirty(nd->claIndexUL) == false){
			for(int i=0;i<nstates*mod->NRateCats();i++) deb << claMan->GetCla(nd->claIndexUL)->arr[index+i] << "\t";
			deb << claMan->GetCla(nd->claIndexUL)->underflow_mult[site];
			}
		deb <<"\n";
		}
	if(nd->IsInternal()){
		deb << nd->nodeNum << "\t2\t" << nd->claIndexUR << "\t";
		if(claMan->IsDirty(nd->claIndexUR) == false){
			for(int i=0;i<nstates*mod->NRateCats();i++) deb << claMan->GetCla(nd->claIndexUR)->arr[index+i] << "\t";
			deb << claMan->GetCla(nd->claIndexUR)->underflow_mult[site];
			}
		deb <<"\n";
		}

	//this version only outputs clean clas
/*	if(nd->IsInternal() && claMan->IsDirty(nd->claIndexDown) == false){
		deb << nd->nodeNum << "\t0\t" << nd->claIndexDown << "\t";
		for(int i=0;i<nstates*mod->NRateCats();i++) deb << claMan->GetCla(nd->claIndexDown)->arr[index+i] << "\t";
		deb << claMan->GetCla(nd->claIndexDown)->underflow_mult[site] <<"\n";
		}
	if(nd->IsInternal() && claMan->IsDirty(nd->claIndexUL) == false){
		deb << nd->nodeNum << "\t1\t" << nd->claIndexUL << "\t";
		for(int i=0;i<nstates*mod->NRateCats();i++) deb << claMan->GetCla(nd->claIndexUL)->arr[index+i] << "\t";
		deb << claMan->GetCla(nd->claIndexUL)->underflow_mult[site] <<"\n";
		}
	if(nd->IsInternal() && claMan->IsDirty(nd->claIndexUR) == false){
		deb << nd->nodeNum << "\t2\t" << nd->claIndexUR << "\t";
		for(int i=0;i<nstates*mod->NRateCats();i++) deb << claMan->GetCla(nd->claIndexUR)->arr[index+i] << "\t";
		deb << claMan->GetCla(nd->claIndexUR)->underflow_mult[site] <<"\n";
		}
*/
	if(nd->IsInternal())
		OutputNthClaAcrossTree(deb, nd->left, site);
	if(nd->next!=NULL)
		OutputNthClaAcrossTree(deb, nd->next, site);
	}

void Tree::CountNumReservedClas(int &clean, int &tempRes, int&res){
	clean=0;
	tempRes=0;
	res=0;

	if(claMan->IsDirty(allNodes[0]->claIndexDown)==false){
		clean++;
		res += (claMan->IsClaReserved(allNodes[0]->claIndexDown));
		tempRes += (claMan->IsClaTempReserved(allNodes[0]->claIndexDown));
		}
	if(claMan->IsDirty(allNodes[0]->claIndexUL)==false){
		clean++;
		res += (claMan->IsClaReserved(allNodes[0]->claIndexUL));
		tempRes += (claMan->IsClaTempReserved(allNodes[0]->claIndexUL));
		}
	if(claMan->IsDirty(allNodes[0]->claIndexUR)==false){
		clean++;
		res += (claMan->IsClaReserved(allNodes[0]->claIndexUR));
		tempRes += (claMan->IsClaTempReserved(allNodes[0]->claIndexUR));
		}
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		if(claMan->IsDirty(allNodes[i]->claIndexDown)==false){
			clean++;
			res += (claMan->IsClaReserved(allNodes[i]->claIndexDown));
			tempRes += (claMan->IsClaTempReserved(allNodes[i]->claIndexDown));
			}
		if(claMan->IsDirty(allNodes[i]->claIndexUL)==false){
			clean++;
			res += (claMan->IsClaReserved(allNodes[i]->claIndexUL));
			tempRes += (claMan->IsClaTempReserved(allNodes[i]->claIndexUL));
			}
		if(claMan->IsDirty(allNodes[i]->claIndexUR)==false){
			clean++;
			res += (claMan->IsClaReserved(allNodes[i]->claIndexUR));
			tempRes += (claMan->IsClaTempReserved(allNodes[i]->claIndexUR));
			}
		}
	}

void Tree::SetupClasForSubtreeMode(int subtreeNode){
	TreeNode *subnode=allNodes[subtreeNode];

	claMan->ReserveCla(subnode->claIndexDown, false);
	claMan->ReserveCla(subnode->claIndexUL, false);
	claMan->ReserveCla(subnode->claIndexUR, false);

	if(subnode->anc != root){
		if(subnode->anc->left==subnode) claMan->ReserveCla(subnode->anc->claIndexUL, false);
		else if(subnode->anc->right==subnode) claMan->ReserveCla(subnode->anc->claIndexUR, false);
		}

	DirtyNodesOutsideOfSubtree(root, subtreeNode);
	}

void Tree::DirtyNodesOutsideOfSubtree(TreeNode *nd, int subtreeNode){

	if(nd != root){
		claMan->ReclaimSingleCla(nd->claIndexDown);
		claMan->ReclaimSingleCla(nd->claIndexUL);
		claMan->ReclaimSingleCla(nd->claIndexUR);
		}

	if(nd->left->IsInternal() && nd->left->nodeNum != subtreeNode && nd->left->nodeNum != allNodes[subtreeNode]->anc->nodeNum){
		DirtyNodesOutsideOfSubtree(nd->left, subtreeNode);
		}
	if(nd->right->IsInternal() && nd->right->nodeNum != subtreeNode && nd->right->nodeNum != allNodes[subtreeNode]->anc->nodeNum){
		DirtyNodesOutsideOfSubtree(nd->right, subtreeNode);
		}
	if(nd->IsRoot() && nd->left->next->IsInternal() && nd->left->next->nodeNum != subtreeNode && nd->left->next->nodeNum != allNodes[subtreeNode]->anc->nodeNum){
		DirtyNodesOutsideOfSubtree(nd->left->next, subtreeNode);
		}
	}

void Tree::OutputValidClaIndeces(){
	ofstream cla("claind.log");
	if(claMan->IsDirty(allNodes[0]->claIndexDown)==false){
		cla << "0\t" << allNodes[0]->claIndexDown << "\t" << claMan->GetNumAssigned(allNodes[0]->claIndexDown) << "\t" << claMan->GetReclaimLevel(allNodes[0]->claIndexDown) << "\t" << claMan->IsClaReserved(allNodes[0]->claIndexDown) <<"\n";
		}
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		cla << i << "\t" << allNodes[i]->claIndexDown << "\t" << claMan->GetNumAssigned(allNodes[i]->claIndexDown) << "\t" << claMan->GetReclaimLevel(allNodes[i]->claIndexDown) << "\t" << claMan->IsClaReserved(allNodes[i]->claIndexDown) << "\n";
		}
	cla.close();
	}

void Tree::GetInternalStateString(char *string, int nodeNum){
	assert(0);
//	Score(nodeNum);
//	InferStatesFromCla(string, claMan->GetTempCla()->arr, data->NChar());
	}

void Tree::InferAllInternalStateProbs(const char *ofprefix){
	char filename[80];
	sprintf(filename, "%s.internalstates.log", ofprefix);
	ofstream out(filename);
	out.precision(5);
	AssignCLAsFromMaster();
	RecursivelyCalculateInternalStateProbs(root, &out);
	out.close();
	}

void Tree::RecursivelyCalculateInternalStateProbs(TreeNode *nd, ofstream *out){
	if(nd->IsInternal())
		RecursivelyCalculateInternalStateProbs(nd->left, out);
	if (nd->next)
		RecursivelyCalculateInternalStateProbs(nd->next, out);

	if(nd->IsInternal()){
		int wholeTreeIndex=ConditionalLikelihoodRateHet(ROOT, nd, true);
		vector<InternalState *> *stateProbs=InferStatesFromCla(claMan->GetCla(wholeTreeIndex)->arr, data->NChar(), mod->NRateCats());

		if (out) {
			char subtreeString[50000];
			nd->MakeNewickForSubtree(subtreeString);

			*out << "node " << nd->nodeNum << "\t" << subtreeString << "\t";
			char *loc=subtreeString;
			NxsString temp;
	
			while(*loc){
				if (isdigit(*loc) == false)
					*out << *loc++;
				else{
					while(isdigit(*loc))
						temp += *loc++;
					*out << data->TaxonLabel(atoi(temp.c_str())-1);
					temp="";
					}
				}
			*out << "\n";
	
			for(int s=0;s<data->GapsIncludedNChar();s++){
				*out << s+1 << "\t";
				if(data->Number(s) > -1)
					(*stateProbs)[data->Number(s)]->Output(*out);
				else
					*out << "Entirely uninformative character (gaps,N's or ?'s)\n";
				}
			}
		claMan->ClearTempReservation(wholeTreeIndex);
		claMan->DecrementCla(wholeTreeIndex);

		for(vector<InternalState*>::iterator delit=stateProbs->begin();delit!=stateProbs->end();delit++){
			delete *(delit);
			}
		delete stateProbs;
		}
	}

void Tree::ClaReport(ofstream &cla){
	int totDown=0;
	int totUL=0;
	int totUR=0;

	cla << "root\t" << claMan->GetReclaimLevel(root->claIndexDown) << "\t" << claMan->GetNumAssigned(root->claIndexDown)<< "\t" << claMan->GetClaNumber(root->claIndexDown);
	cla << "\n\t" << claMan->GetReclaimLevel(root->claIndexUL) << "\t" << claMan->GetNumAssigned(root->claIndexUL) << "\t" << claMan->GetClaNumber(root->claIndexUL);
	cla << "\n\t" << claMan->GetReclaimLevel(root->claIndexUR)  << "\t" << claMan->GetNumAssigned(root->claIndexUR)  << "\t" << claMan->GetClaNumber(root->claIndexUR) << "\n";
//	cla << "\t" << claMan->GetNumAssigned(root->claIndexDown) << "\t" << claMan->GetNumAssigned(root->claIndexUL) << "\t" << claMan->GetNumAssigned(root->claIndexUR)  << "\n";
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		TreeNode *n=allNodes[i];
	cla << i << "\t" << claMan->GetReclaimLevel(n->claIndexDown) << "\t" << claMan->GetNumAssigned(n->claIndexDown) << "\t" << claMan->GetClaNumber(n->claIndexDown);
	cla << "\n\t" << claMan->GetReclaimLevel(n->claIndexUL) << "\t" << claMan->GetNumAssigned(n->claIndexUL) << "\t" << claMan->GetClaNumber(n->claIndexUL);
	cla << "\n\t" << claMan->GetReclaimLevel(n->claIndexUR)  << "\t" << claMan->GetNumAssigned(n->claIndexUR)  << "\t" << claMan->GetClaNumber(n->claIndexUR) << "\n";
		totDown += claMan->GetReclaimLevel(n->claIndexDown);
		totUL += claMan->GetReclaimLevel(n->claIndexUL);
		totUR += claMan->GetReclaimLevel(n->claIndexUR);
		}
	cla << "tots\t" << totDown << "\t" << totUL << "\t" << totUR << endl;
//	cla.close();
	}

FLOAT_TYPE Tree::CountClasInUse(){
	FLOAT_TYPE inUse=0.0;

	if(claMan->IsDirty(root->claIndexDown) == false) inUse += ONE_POINT_ZERO/claMan->GetNumAssigned(root->claIndexDown);
	if(claMan->IsDirty(root->claIndexUL) == false) inUse += ONE_POINT_ZERO/claMan->GetNumAssigned(root->claIndexUL);
	if(claMan->IsDirty(root->claIndexUR) == false) inUse += ONE_POINT_ZERO/claMan->GetNumAssigned(root->claIndexUR);
	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		TreeNode *n=allNodes[i];
		if(claMan->IsDirty(n->claIndexDown) == false) inUse += ONE_POINT_ZERO/claMan->GetNumAssigned(n->claIndexDown);
		if(claMan->IsDirty(n->claIndexUL) == false) inUse += ONE_POINT_ZERO/claMan->GetNumAssigned(n->claIndexUL);
		if(claMan->IsDirty(n->claIndexUR) == false) inUse += ONE_POINT_ZERO/claMan->GetNumAssigned(n->claIndexUR);
		}
	return inUse;
	}

void Tree::OutputSiteLikelihoods(vector<double> &likes, const int *under1, const int *under2, ofstream &ordered, ofstream &packed){
	assert(likes.size() == data->NChar());
	ordered << "site#\ttruelnL\tunder1\tunder2" << endl;
	packed << "packedIndex\ttruelnL\tunder1\tunder2" << endl;
	ordered.precision(10);
	packed.precision(10);
	
	for(int site = 0;site < data->GapsIncludedNChar();site++){
		int col = data->Number(site);
		if(col == -1)
			ordered << site+1 << "\tgap\t-\t-";
		else{
			ordered << site+1 << "\t" << likes[col] << "\t" << under1[col];
			if(under2 != NULL)
				ordered << "\t" << under2[col] << endl;
			else
				ordered << "\t-" << endl;
			}
		}

	// output to std::cerr only for iGarli hacking...
	std::cerr << "[igarlisitelike ";
	for(int c = 0;c < data->NChar();c++){
		std::cerr << likes[c] << ' ';
		packed << c << "\t" << likes[c] << "\t" << under1[c];
		if(under2 != NULL)
			packed << "\t" << under2[c] << endl;
		else
			packed << "\t-" << endl;
		}
	std::cerr << "]\n";
	}

void Tree::OutputSiteDerivatives(vector<double> &likes, vector<double> &d1s, vector<double> &d2s, const int *under1, const int *under2, ofstream &ordered, ofstream &packed){
	assert(likes.size() == data->NChar());
	ordered << "site#\ttruelnL\td1\td2\tunder1\tunder2" << endl;
	packed << "packedIndex\ttruelnL\td1\td2\tunder1\tunder2" << endl;
	ordered.precision(10);
	packed.precision(10);

	for(int site = 0;site < data->GapsIncludedNChar();site++){
		int col = data->Number(site);
		if(col == -1)
			ordered << site+1 << "\tgap\t-\t-\t-\t-";
		else{
			ordered << site+1 << "\t" << likes[col] << "\t" << d1s[col] << "\t" << d2s[col] << "\t" << under1[col];
			if(under2 != NULL)
				ordered << "\t" << under2[col] << endl;
			else
				ordered << "\t-" << endl;
			}
		}
	for(int c = 0;c < data->NChar();c++){
		packed << c << "\t" << likes[c] << "\t" << d1s[c] << "\t" << d2s[c] << "\t" << under1[c];
		if(under2 != NULL)
			packed << "\t" << under2[c] << endl;
		else
			packed << "\t-" << endl;
		}
	}

FLOAT_TYPE Tree::GetScorePartialTerminalNState(const CondLikeArray *partialCLA, const FLOAT_TYPE *prmat, const char *Ldat){

	//this function assumes that the pmat is arranged with the nstates^2 entries for the
	//first rate, followed by nstates^2 for the second, etc.
	const FLOAT_TYPE *partial=partialCLA->arr;
	const int *underflow_mult=partialCLA->underflow_mult;

	const int nstates = mod->NStates();
	const int nRateCats = mod->NRateCats();
	const int nchar = data->NChar();
	const int *countit=data->GetCounts();
	const char *Ldata = Ldat;

	const FLOAT_TYPE *rateProb=mod->GetRateProbs();
	const int lastConst=data->LastConstant();
	const int *conStates=data->GetConstStates();
	const FLOAT_TYPE prI=mod->PropInvar();

#	ifdef UNIX
		madvise((void*)partial, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#	endif

	FLOAT_TYPE siteL, totallnL=ZERO_POINT_ZERO, unscaledlnL, grandSumlnL=ZERO_POINT_ZERO;

	FLOAT_TYPE *freqs = new FLOAT_TYPE[nstates];
	for(int i=0;i<nstates;i++) freqs[i]=mod->StateFreq(i);

	vector<double> siteLikes;
	const bool doOutputSiteLikes = Population::GetOutputSiteLikes();
	if(siteToScore > 0)
		Ldat += siteToScore;

	if(nRateCats == 1) {
#		ifdef OMP_TERMSCORE_NSTATE
#			ifdef LUMP_LIKES
#				pragma omp parallel for private(partial, Ldata, siteL, unscaledlnL) reduction(+ : totallnL, grandSumlnL)
#			else
#				pragma omp parallel for private(partial, Ldata, siteL, unscaledlnL) reduction(+ : totallnL)
#			endif
#		endif

		for(int i=0;i<nchar;i++) {

#			ifdef OMP_TERMSCORE_NSTATE
				Ldata = &Ldat[i];
				partial = &partialCLA->arr[i*nstates];
#			endif
#			ifdef USE_COUNTS_IN_BOOT
				if(countit[i] > 0){
#			else
				if(1){
#			endif
				siteL = 0.0;
				if(*Ldata < nstates){ //no ambiguity
					for(int from=0;from<nstates;from++){
						siteL += prmat[(*Ldata)+nstates*from] * partial[from] * freqs[from];
						}
					partial += nstates;
					}

				else if(*Ldata == nstates){ //total ambiguity
					for(int from=0;from<nstates;from++){
						siteL += partial[from] * freqs[from];
						}
					partial += nstates;
					}
				else{ //partial ambiguity
					assert(0);
					}
				siteL *= rateProb[0];//multiply by (1-pinv)
				if((mod->NoPinvInModel() == false) && (i<=lastConst)){
					if(underflow_mult[i] == 0)
						siteL += prI*freqs[conStates[i]];
					else
						siteL += prI*freqs[conStates[i]]*exp((FLOAT_TYPE)underflow_mult[i]);
					}
				unscaledlnL = (log(siteL) - underflow_mult[i]);
				assert(siteL > ZERO_POINT_ZERO);//this should be positive
				assert(unscaledlnL < 1.0e-4);//this should be negative or zero
				//rounding error in multiplying a site that is fully ambiguous across the tree
				//(which might not have been removed from the data because we are only scoring a
				//partial tree during stepwise addition) can cause the unscaledlnL to be slightly
				//> zero.  If that is the case, just ignore it
				if(unscaledlnL < ZERO_POINT_ZERO)
					totallnL += (countit[i] * unscaledlnL);

#				ifdef ALLOW_SINGLE_SITE
					if(siteToScore > -1)
						break;
#				endif
				}
			else{//nothing needs to be done if the count for this site is 0
				}
			Ldata++;
#			ifdef LUMP_LIKES
				if((i + 1) % LUMP_FREQ == 0){
					grandSumlnL += totallnL;
					totallnL = ZERO_POINT_ZERO;
					}
#			endif
			if (doOutputSiteLikes)
				siteLikes.push_back(unscaledlnL);
			}
		if (doOutputSiteLikes) {
			ofstream ord("orderedSiteLikes.log");
			ofstream packed("packedSiteLikes.log");
			OutputSiteLikelihoods(siteLikes, underflow_mult, NULL, ord, packed);
			ord.close();
			packed.close();
			}
		}
	else{//multiple rates
		FLOAT_TYPE rateL;
#		ifdef OMP_TERMSCORE_NSTATE
#			ifdef LUMP_LIKES
#				pragma omp parallel for private(partial, Ldata, siteL, rateL, unscaledlnL) reduction(+ : totallnL, grandSumlnL)
#			else
#				pragma omp parallel for private(partial, Ldata, siteL, rateL, unscaledlnL) reduction(+ : totallnL)
#			endif
#		endif

		for(int i=0;i<nchar;i++){

#			ifdef OMP_TERMSCORE_NSTATE
				Ldata = &Ldat[i];
				partial = &partialCLA->arr[i*nstates*nRateCats];
#			endif
#			ifdef USE_COUNTS_IN_BOOT
				if(countit[i] > 0){
#			else
				if(1){
#			endif
				siteL = ZERO_POINT_ZERO;
				if(*Ldata < nstates){ //no ambiguity
					for(int rate=0;rate<nRateCats;rate++){
						rateL = ZERO_POINT_ZERO;
						const int rateOffset = rate * nstates * nstates;
						for(int from=0;from<nstates;from++){
							const int offset = from * nstates;
							rateL += prmat[rateOffset + offset + (*Ldata)] * partial[from] * freqs[from];
							}
						siteL += rateL * rateProb[rate];
						partial += nstates;
						}
					}
				else{ //total ambiguity
					for(int rate=0;rate<nRateCats;rate++){
						rateL = ZERO_POINT_ZERO;
						for(int from=0;from<nstates;from++){
							rateL += partial[from] * freqs[from];
							}
						siteL += rateL * rateProb[rate];
						partial += nstates;
						}
					}

				if((mod->NoPinvInModel() == false) && (i<=lastConst)){
					if(underflow_mult[i] == 0)
						siteL += prI*freqs[conStates[i]];
					else
						siteL += prI*freqs[conStates[i]]*exp((FLOAT_TYPE)underflow_mult[i]);
					}

				FLOAT_TYPE unscaledlnL = (log(siteL) - underflow_mult[i]);
				assert(siteL > ZERO_POINT_ZERO);//this should be positive
				assert(unscaledlnL < 1.0e-4);//this should be negative or zero
				//rounding error in multiplying a site that is fully ambiguous across the tree
				//(which might not have been removed from the data because we are only scoring a
				//partial tree during stepwise addition) can cause the unscaledlnL to be slightly
				//> zero.  If that is the case, just ignore it
				if(unscaledlnL < ZERO_POINT_ZERO)
					totallnL += (countit[i] * unscaledlnL);

#				ifdef ALLOW_SINGLE_SITE
					if(siteToScore > -1)
						break;
#				endif
				}
			Ldata++;
#			ifdef LUMP_LIKES
				if((i + 1) % LUMP_FREQ == 0){
					grandSumlnL += totallnL;
					totallnL = ZERO_POINT_ZERO;
					}
#			endif
			if (Population::GetOutputSiteLikes())
				siteLikes.push_back(unscaledlnL);
			}
		if (Population::GetOutputSiteLikes()) {
			ofstream ord("orderedSiteLikes.log");
			ofstream packed("packedSiteLikes.log");
			OutputSiteLikelihoods(siteLikes, underflow_mult, NULL, ord, packed);
			ord.close();
			packed.close();
			}
		}
#	ifdef LUMP_LIKES
		totallnL += grandSumlnL;
#	endif

	delete []freqs;
	return totallnL;
	}

FLOAT_TYPE Tree::GetScorePartialTerminalRateHet(const CondLikeArray *partialCLA, const FLOAT_TYPE *prmat, const char *Ldata){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	const FLOAT_TYPE *partial=partialCLA->arr;
	const int *underflow_mult=partialCLA->underflow_mult;
	const int nRateCats=mod->NRateCats();

	const int nchar=data->NChar();

	const int *countit=data->GetCounts();

	const FLOAT_TYPE *rateProb=mod->GetRateProbs();

	const int lastConst=data->LastConstant();
	const int *conBases=data->GetConstStates();
	const FLOAT_TYPE prI=mod->PropInvar();

	FLOAT_TYPE freqs[4];
	for(int i=0;i<4;i++) freqs[i]=mod->StateFreq(i);

#ifdef UNIX
	madvise((void*)partial, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

#ifdef ALLOW_SINGLE_SITE
	if(siteToScore > 0) Ldata = AdvanceDataPointer(Ldata, siteToScore);
#endif

	FLOAT_TYPE siteL, totallnL=ZERO_POINT_ZERO, grandSumlnL=ZERO_POINT_ZERO, unscaledlnL;
	FLOAT_TYPE La, Lc, Lg, Lt;

	vector<double> siteLikes;
	const bool doOutputSiteLikes = Population::GetOutputSiteLikes();
	for(int i=0;i<nchar;i++){
#ifdef USE_COUNTS_IN_BOOT
		if(countit[i] > 0){
#else
		if(1){
#endif
			La=Lc=Lg=Lt=ZERO_POINT_ZERO;
			if(*Ldata > -1){ //no ambiguity
				for(int i=0;i<nRateCats;i++){
					La  += prmat[(*Ldata)+16*i] * partial[0] * rateProb[i];
					Lc  += prmat[(*Ldata+4)+16*i] * partial[1] * rateProb[i];
					Lg  += prmat[(*Ldata+8)+16*i] * partial[2] * rateProb[i];
					Lt  += prmat[(*Ldata+12)+16*i] * partial[3] * rateProb[i];
					partial += 4;
					}
				Ldata++;
				}

			else if(*Ldata == -4){ //total ambiguity
				for(int i=0;i<nRateCats;i++){
					La += partial[0] * rateProb[i];
					Lc += partial[1] * rateProb[i];
					Lg += partial[2] * rateProb[i];
					Lt += partial[3] * rateProb[i];
					partial += 4;
					}
				Ldata++;
				}
			else{ //partial ambiguity
				char nstates=-1 * *(Ldata++);
				for(int i=0;i<nstates;i++){
					for(int i=0;i<nRateCats;i++){
						La += prmat[(*Ldata)+16*i]  * partial[4*i] * rateProb[i];
						Lc += prmat[(*Ldata+4)+16*i] * partial[1+4*i] * rateProb[i];
						Lg += prmat[(*Ldata+8)+16*i]* partial[2+4*i] * rateProb[i];
						Lt += prmat[(*Ldata+12)+16*i]* partial[3+4*i] * rateProb[i];
						}
					Ldata++;
					}
				partial+=4*nRateCats;
				}
			if((mod->NoPinvInModel() == false) && (i<=lastConst)){
				FLOAT_TYPE btot=0.0;
				if(conBases[i]&1) btot+=freqs[0];
				if(conBases[i]&2) btot+=freqs[1];
				if(conBases[i]&4) btot+=freqs[2];
				if(conBases[i]&8) btot+=freqs[3];
				if(underflow_mult[i]==0)
					siteL  = ((La*freqs[0]+Lc*freqs[1]+Lg*freqs[2]+Lt*freqs[3]) + prI*btot);
				else
					siteL  = ((La*freqs[0]+Lc*freqs[1]+Lg*freqs[2]+Lt*freqs[3]) + (prI*btot*exp((FLOAT_TYPE)underflow_mult[i])));
				}
			else
				siteL  = ((La*freqs[0]+Lc*freqs[1]+Lg*freqs[2]+Lt*freqs[3]));
			unscaledlnL = (log(siteL) - underflow_mult[i]);
			totallnL += (countit[i] * unscaledlnL);

#ifdef ALLOW_SINGLE_SITE
			if(siteToScore > -1) break;
#endif
			}
		else{
#ifdef OPEN_MP
			//this is a little strange, but partial only needs to be advanced in the case of OMP
			//because sections of the CLAs corresponding to sites with count=0 are skipped
			//over in OMP instead of being eliminated
			partial += 4*nRateCats;
#endif
			if(*Ldata > -1 || *Ldata == -4) Ldata++;
			else{
				int states = -1 * *Ldata;
				do{
					Ldata++;
					}while (states-- > 0);
				}
			}
#ifdef LUMP_LIKES
		if((i + 1) % LUMP_FREQ == 0){
			grandSumlnL += totallnL;
			totallnL = ZERO_POINT_ZERO;
			}
#endif
		if (doOutputSiteLikes)
			siteLikes.push_back(unscaledlnL);
		}
	if (doOutputSiteLikes) {
		ofstream ord("orderedSiteLikes.log");
		ofstream packed("packedSiteLikes.log");
		OutputSiteLikelihoods(siteLikes, underflow_mult, NULL, ord, packed);
		ord.close();
		packed.close();
	}
#ifdef LUMP_LIKES
	totallnL += grandSumlnL;
#endif
	return totallnL;
	}

FLOAT_TYPE Tree::GetScorePartialInternalRateHet(const CondLikeArray *partialCLA, const CondLikeArray *childCLA, const FLOAT_TYPE *prmat){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	const FLOAT_TYPE *CL1=childCLA->arr;
	const FLOAT_TYPE *partial=partialCLA->arr;
	const int *underflow_mult1=partialCLA->underflow_mult;
	const int *underflow_mult2=childCLA->underflow_mult;

	const int nchar=data->NChar();
	const int nRateCats=mod->NRateCats();

	const int *countit=data->GetCounts();

	const FLOAT_TYPE *rateProb=mod->GetRateProbs();
	const FLOAT_TYPE prI=mod->PropInvar();
	const int lastConst=data->LastConstant();
	const int *conBases=data->GetConstStates();

	FLOAT_TYPE freqs[4];
	for(int i=0;i<4;i++) freqs[i]=mod->StateFreq(i);


#ifdef UNIX
	madvise((void*)partial, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void*)CL1, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

	FLOAT_TYPE siteL, totallnL=ZERO_POINT_ZERO, grandSumlnL=ZERO_POINT_ZERO, unscaledlnL;
	FLOAT_TYPE La, Lc, Lg, Lt;

	vector<double> siteLikes;
	const bool doOutputSiteLikes = Population::GetOutputSiteLikes();
	for(int i=0;i<nchar;i++){
#ifdef USE_COUNTS_IN_BOOT
		if(countit[i] > 0){
#else
		if(1){
#endif
			La=Lc=Lg=Lt=ZERO_POINT_ZERO;
			for(int r=0;r<nRateCats;r++){
				int rOff=r*16;
				La += ( prmat[rOff ]*CL1[0]+prmat[rOff + 1]*CL1[1]+prmat[rOff + 2]*CL1[2]+prmat[rOff + 3]*CL1[3]) * partial[0] * rateProb[r];
				Lc += ( prmat[rOff + 4]*CL1[0]+prmat[rOff + 5]*CL1[1]+prmat[rOff + 6]*CL1[2]+prmat[rOff + 7]*CL1[3]) * partial[1] * rateProb[r];
				Lg += ( prmat[rOff + 8]*CL1[0]+prmat[rOff + 9]*CL1[1]+prmat[rOff + 10]*CL1[2]+prmat[rOff + 11]*CL1[3]) * partial[2] * rateProb[r];
				Lt += ( prmat[rOff + 12]*CL1[0]+prmat[rOff + 13]*CL1[1]+prmat[rOff + 14]*CL1[2]+prmat[rOff + 15]*CL1[3]) * partial[3] * rateProb[r];
				partial+=4;
				CL1+=4;
				}
			if((mod->NoPinvInModel() == false) && (i<=lastConst)){
				FLOAT_TYPE btot=ZERO_POINT_ZERO;
				if(conBases[i]&1) btot+=freqs[0];
				if(conBases[i]&2) btot+=freqs[1];
				if(conBases[i]&4) btot+=freqs[2];
				if(conBases[i]&8) btot+=freqs[3];
				if(underflow_mult1[i] + underflow_mult2[i] == 0)
					siteL  = ((La*freqs[0]+Lc*freqs[1]+Lg*freqs[2]+Lt*freqs[3]) + prI*btot);
				else
					siteL  = ((La*freqs[0]+Lc*freqs[1]+Lg*freqs[2]+Lt*freqs[3]) + (prI*btot*exp((FLOAT_TYPE)underflow_mult1[i]+underflow_mult2[i])));
				}
			else
				siteL  = ((La*freqs[0]+Lc*freqs[1]+Lg*freqs[2]+Lt*freqs[3]));

			unscaledlnL = (log(siteL) - underflow_mult1[i] - underflow_mult2[i]);
			totallnL += (countit[i] * unscaledlnL);

#ifdef ALLOW_SINGLE_SITE
			if(siteToScore > -1) break;
#endif
			}
		else{
#ifdef OPEN_MP
			//this is a little strange, but the arrays only needs to be advanced in the case of OMP
			//because sections of the CLAs corresponding to sites with count=0 are skipped
			//over in OMP instead of being eliminated
			partial+=4*nRateCats;
			CL1+=4*nRateCats;
#endif
			}
#ifdef LUMP_LIKES
		if((i + 1) % LUMP_FREQ == 0){
			grandSumlnL += totallnL;
			totallnL = ZERO_POINT_ZERO;
			}
#endif
		if (doOutputSiteLikes)
			siteLikes.push_back(unscaledlnL);
		}
	if (doOutputSiteLikes) {
		ofstream ord("orderedSiteLikes.log");
		ofstream packed("packedSiteLikes.log");
		OutputSiteLikelihoods(siteLikes, underflow_mult1, underflow_mult2, ord, packed);
		ord.close();
		packed.close();
		}
#ifdef LUMP_LIKES
	totallnL += grandSumlnL;
#endif

	return totallnL;
	}

FLOAT_TYPE Tree::GetScorePartialInternalNState(const CondLikeArray *partialCLA, const CondLikeArray *childCLA, const FLOAT_TYPE *prmat){
	//this function assumes that the pmat is arranged with nstates^2 entries for the
	//first rate, followed by nstate^2 for the second, etc.
	const FLOAT_TYPE *CL1=childCLA->arr;
	const FLOAT_TYPE *partial=partialCLA->arr;
	const int *underflow_mult1=partialCLA->underflow_mult;
	const int *underflow_mult2=childCLA->underflow_mult;

	const int nchar=data->NChar();
	const int *countit=data->GetCounts();
	const int nRateCats = mod->NRateCats();
	const int nstates = mod->NStates();

	const FLOAT_TYPE *rateProb=mod->GetRateProbs();
	const FLOAT_TYPE prI=mod->PropInvar();
	const int lastConst=data->LastConstant();
	const int *conStates=data->GetConstStates();

#ifdef UNIX
	madvise((void*)partial, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void*)CL1, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

	FLOAT_TYPE totallnL=ZERO_POINT_ZERO, siteL, unscaledlnL, grandSumlnL=ZERO_POINT_ZERO;

	FLOAT_TYPE *freqs = new FLOAT_TYPE[nstates];
	for(int i=0;i<nstates;i++) freqs[i]=mod->StateFreq(i);

	vector<double> siteLikes;
	const bool doOutputSiteLikes = Population::GetOutputSiteLikes();

	if(nRateCats == 1){
#ifdef OMP_INTSCORE_NSTATE
	#ifdef LUMP_LIKES
	#pragma omp parallel for private(partial, CL1, siteL, unscaledlnL) reduction(+ : totallnL, grandSumlnL)
	#else
	#pragma omp parallel for private(partial, CL1, siteL, unscaledlnL) reduction(+ : totallnL)
	#endif
		for(int i=0;i<nchar;i++){
			partial = &(partialCLA->arr[nstates*i]);
			CL1		= &(childCLA->arr[nstates*i]);
#else
		for(int i=0;i<nchar;i++){
#endif

#ifdef USE_COUNTS_IN_BOOT
			if(countit[i] > 0){
#else
			if(1){
#endif
				siteL = 0.0;
				for(int from=0;from<nstates;from++){
					FLOAT_TYPE temp = 0.0;
					for(int to=0;to<nstates;to++){
						temp += prmat[from*nstates + to]*CL1[to];
						}
					siteL += temp * partial[from] * freqs[from];
					}
				siteL *= rateProb[0]; //multiply by (1-pinv)
				if((mod->NoPinvInModel() == false) && (i<=lastConst)){
					if(underflow_mult1[i] + underflow_mult2[i] == 0)
						siteL += prI*freqs[conStates[i]];
					else
						siteL += prI*freqs[conStates[i]]*exp((FLOAT_TYPE)underflow_mult1[i]+(FLOAT_TYPE)underflow_mult2[i]);
					}
				CL1 += nstates;
				partial += nstates;

				unscaledlnL = (log(siteL) - underflow_mult1[i] - underflow_mult2[i]);
				assert(siteL > ZERO_POINT_ZERO);//this should be positive
				assert(unscaledlnL < 1.0e-4);//this should be negative or zero
				//rounding error in multiplying a site that is fully ambiguous across the tree
				//(which might not have been removed from the data because we are only scoring a
				//partial tree during stepwise addition) can cause the unscaledlnL to be slightly
				//> zero.  If that is the case, just ignore it
				if(unscaledlnL < ZERO_POINT_ZERO)
					totallnL += (countit[i] * unscaledlnL);

#ifdef ALLOW_SINGLE_SITE
				if(siteToScore > -1) break;
#endif
				}
			else{//nothing needs to be done if the count for this site is 0
				}
#ifdef LUMP_LIKES
			if((i + 1) % LUMP_FREQ == 0){
				grandSumlnL += totallnL;
				totallnL = ZERO_POINT_ZERO;
				}
#endif
			if (doOutputSiteLikes)
				siteLikes.push_back(unscaledlnL);
			}
		if (doOutputSiteLikes) {
			ofstream ord("orderedSiteLikes.log");
			ofstream packed("packedSiteLikes.log");
			OutputSiteLikelihoods(siteLikes, underflow_mult1, underflow_mult2, ord, packed);
			ord.close();
			packed.close();
			}
		}
	else{
		FLOAT_TYPE siteL, tempL, rateL;

#ifdef OMP_INTSCORE_NSTATE
	#ifdef LUMP_LIKES
	#pragma omp parallel for private(partial, CL1, siteL, tempL, rateL, unscaledlnL) reduction(+ : totallnL, grandSumlnL)
	#else
	#pragma omp parallel for private(partial, CL1, siteL, tempL, rateL, unscaledlnL) reduction(+ : totallnL)
	#endif
		for(int i=0;i<nchar;i++){
			partial = &(partialCLA->arr[nRateCats*nstates*i]);
			CL1		= &(childCLA->arr[nRateCats*nstates*i]);
#else
		for(int i=0;i<nchar;i++){
#endif
#ifdef USE_COUNTS_IN_BOOT
			if(countit[i] > 0){
#else
			if(1){
#endif
				siteL = ZERO_POINT_ZERO;
				for(int rate=0;rate<nRateCats;rate++){
					rateL = ZERO_POINT_ZERO;
					int rateOffset = rate*nstates*nstates;
					for(int from=0;from<nstates;from++){
						tempL = ZERO_POINT_ZERO;
						int offset = from * nstates;
						for(int to=0;to<nstates;to++){
							tempL += prmat[rateOffset + offset + to]*CL1[to];
							}
						rateL += tempL * partial[from] * freqs[from];
						}
					siteL += rateL * rateProb[rate];
					partial += nstates;
					CL1 += nstates;
					}

				if((mod->NoPinvInModel() == false) && (i<=lastConst)){
					if(underflow_mult1[i] + underflow_mult2[i] == 0)
						siteL += prI*freqs[conStates[i]];
					else
						siteL += prI*freqs[conStates[i]]*exp((FLOAT_TYPE)underflow_mult1[i]+(FLOAT_TYPE)underflow_mult2[i]);
					}
				FLOAT_TYPE unscaledlnL = (log(siteL) - underflow_mult1[i] - underflow_mult2[i]);
				assert(siteL > ZERO_POINT_ZERO);//this should be positive
				assert(unscaledlnL < 1.0e-4);//this should be negative or zero
				//rounding error in multiplying a site that is fully ambiguous across the tree
				//(which might not have been removed from the data because we are only scoring a
				//partial tree during stepwise addition) can cause the unscaledlnL to be slightly
				//> zero.  If that is the case, just ignore it
				if(unscaledlnL < ZERO_POINT_ZERO)
					totallnL += (countit[i] * unscaledlnL);

#ifdef ALLOW_SINGLE_SITE
				if(siteToScore > -1) break;
#endif
				}
			else{ //nothing needs to be done if the count of this site is 0
				}
#ifdef LUMP_LIKES
			if((i + 1) % LUMP_FREQ == 0){
				grandSumlnL += totallnL;
				totallnL = ZERO_POINT_ZERO;
				}
#endif

			if (doOutputSiteLikes)
				siteLikes.push_back(unscaledlnL);
			}
		if (doOutputSiteLikes) {
			ofstream ord("orderedSiteLikes.log");
			ofstream packed("packedSiteLikes.log");
			OutputSiteLikelihoods(siteLikes, underflow_mult1, underflow_mult2, ord, packed);
			ord.close();
			packed.close();
			}

		}
#ifdef LUMP_LIKES
	totallnL += grandSumlnL;
#endif

	delete []freqs;
	return totallnL;
	}

void Tree::LocalMove(){
	assert(0);
	//This is not working
	//this will all assume that there are no polytomies besides the root
	TreeNode *a, *b, *c, *d;
	int cPosition;

	//pick a random TreeNode and set up the rest of the nodes in relation to it//
	int tmp=numTipsTotal+rnd.random_int(numTipsTotal-3)+1;
	TreeNode *u=allNodes[tmp];
	//set up the vicinity of u
	TreeNode *v=u->anc;

	//STANDARDIZE by making v->left=u
	if(u!=v->left){
		if(u==v->left->next){
			if(v->IsRoot()){
				TreeNode *tempnode=v->left;
				TreeNode *tempnode2;
				if(v->left->next==u) tempnode2=u->next;
				else tempnode2=v->left->next;
				v->left=u;
				u->next=tempnode;
				tempnode->next=tempnode2;
				tempnode2->next=NULL;
				}
			else{
				v->RotateDescendents();
			/*	TreeNode *tempnode=v->left;
				v->left=u;
				u->next=tempnode;
				tempnode->next=NULL;
			*/	}
			}
		else{
			//v must be the root, and u must be v->left->next->next
			TreeNode *tempnode=v->left;
			TreeNode *tempnode2=v->left->next;
			v->left=u;
			u->next=tempnode;
			u->next->next=tempnode2;
			tempnode2->next=NULL;
			}
		}
	//determine a and b
	if(rnd.uniform()<.5){
		a=u->left;
		b=a->next;
		}
	else{
		b=u->left;
		a=b->next;
		}
	//STANDARDIZE by making u->left=a
	if(u->left!=a){
		u->RotateDescendents();
/*		u->left=a;
		u->left->next=b;
		b->next=NULL;
*/		}
	//set up the vicinity of v
	if(v->IsRoot()){
		//if v is the root
		if(rnd.uniform()<.05){
			c=u->next;
			d=c->next;
			//STANDARDIZE by making c=v->left->next->next
			u->next=d;
			d->next=c;
			c->next=NULL;
			cPosition=2;
			}
		else{
			d=u->next;
			c=d->next;//left->next->next
			if(c==NULL){
				c=c;
				}
			cPosition=2;
			}
		}
	else{
		//if v is not the root...
		if(rnd.uniform()<.5){
			c=u->next;
			cPosition=1;//left->next
			d=v->anc;
			//STANDARDIZE by making d->left=v if cPosition==1
			if(d->left!=v){
				if(d->anc!=NULL){
					d->RotateDescendents();
/*					TreeNode *tempnode=d->left;
					d->left=v;
					v->next=tempnode;
					tempnode->next=NULL;
*/					}
				else{
					TreeNode *tempnode=d->left;
					TreeNode *tempnode2;
					if(d->left->next==v) tempnode2=v->next;
					else tempnode2=d->left->next;
					d->left=v;
					v->next=tempnode;
					tempnode->next=tempnode2;
					tempnode2->next=NULL;
					}
				}
			}
		else{
			d=u->next;
			c=v->anc;
			cPosition=3;//anc
			//STANDARDIZE by making c->left=v if cPosition==3
			if(c->left!=v){
				if(c->IsRoot()){
					TreeNode *tempnode=c->left;
					TreeNode *tempnode2;
					if(tempnode->next==v) tempnode2=v->next;
					else tempnode2=tempnode->next;
					c->left=v;
					v->next=tempnode;
					tempnode->next=tempnode2;
					tempnode2->next=NULL;
					}
				else{
					c->RotateDescendents();
/*					TreeNode *tempnode=c->left;
					c->left=v;
					v->next=tempnode;
					tempnode->next=NULL;
*/					}
				}
			}
		}
	/*Now that things are set up, we can count on the following being true:
		u->left=a;
		u->left->next=b;
		v->left=a;
		if(v->anc!=NULL){
			v->left->next=c && d->left=v (case 1)
			else c->left=v && v->left->next=d (case 2)
			}
		else{
			v->left->next->next=c && v->left->next=d (case 3)
			}
	*/



	//Ok, the nodes are defined.
	//Calculate the backbone length and the new length
	FLOAT_TYPE m;
	FLOAT_TYPE changing_blens[3];
//	FLOAT_TYPE new_blens[3];
	changing_blens[0]=a->dlen;
	changing_blens[1]=u->dlen;
	if(cPosition==3){
		changing_blens[2]=v->dlen;
		}
	else {
		changing_blens[2]=c->dlen;
		}
	m=changing_blens[0]+changing_blens[1]+changing_blens[2];
	FLOAT_TYPE r=rnd.uniform();



//	FLOAT_TYPE tuning=.25;
//	FLOAT_TYPE tuning=.1;
	FLOAT_TYPE mprime=m*exp((FLOAT_TYPE).5*(rnd.uniform()-(FLOAT_TYPE).5));
	FLOAT_TYPE x, y;
	//choose whether to "detach" u or v.  Don't actually detach anything though
	if(rnd.uniform()<.5){ //detach u
		//calculate x and y
		x=rnd.uniform()*mprime;
		y=(a->dlen+u->dlen) * (mprime/m);

		if(x<y){//all cases
			//no topo change
			a->dlen=x;
			u->dlen=y-x;
			if(cPosition==3) v->dlen=mprime-y;
			else c->dlen = mprime-y;
			TraceDirtynessToRoot(a->anc);
//			tree->AdjustCLArrayFlagsBelow(a->anc, curMove);
			}
		else{
			//case 1
			if(cPosition==1){
				u->left=b;
				u->left->next=c;
				c->next=NULL;
				c->anc=u;
				v->left->next=a;
				a->anc=v;
				a->next=NULL;
				a->dlen=y;
				u->dlen=x-y;
				c->dlen=mprime-x;
				TraceDirtynessToRoot(c->anc);
				//tree->AdjustCLArrayFlagsBelow(c->anc, curMove);
				}
			//case 3
			else if(cPosition==2){
				u->left=b;
				u->left->next=c;
				c->next=NULL;
				c->anc=u;
				v->left->next->next=a;
				a->anc=v;
				a->next=NULL;
				a->dlen=y;
				u->dlen=x-y;
				c->dlen=mprime-x;
				TraceDirtynessToRoot(c->anc);
				//tree->AdjustCLArrayFlagsBelow(c->anc, curMove);
				}
			//case 2
			else{//u and v physically swap positions in this case
				v->left=a;
				a->anc=v;
				a->next=d;
				d->next=NULL;
				u->left=v;
				u->next=v->next;
				v->next=b;
				b->next=NULL;
				u->anc=c;
				v->anc=u;
				c->left=u;
				a->dlen=y;
				v->dlen=x-y;
				u->dlen=mprime-x;
				TraceDirtynessToRoot(a->anc);
				//tree->AdjustCLArrayFlagsBelow(a->anc, curMove);
				}
			}
		}



	else{
		//"detach" v
		x=a->dlen*(mprime/m);
		y=rnd.uniform() * mprime;
		if(x<y){
			//no topo change
			a->dlen=x;
			u->dlen=y-x;
			if(cPosition==3) v->dlen=mprime-y;
			else c->dlen=mprime-y;
			TraceDirtynessToRoot(a->anc);
//			tree->AdjustCLArrayFlagsBelow(a->anc, curMove);
			}
		else{
			//case 1
			if(cPosition==1){
				u->left=b;
				u->left->next=c;
				c->next=NULL;
				c->anc=u;
				v->left->next=a;
				a->anc=v;
				a->next=NULL;
				a->dlen=y;
				u->dlen=x-y;
				c->dlen=mprime-x;
				TraceDirtynessToRoot(c->anc);
		//		tree->AdjustCLArrayFlagsBelow(c->anc, curMove);
				}
			//case 3
			else if(cPosition==2){
				u->left=b;
				u->left->next=c;
				c->next=NULL;
				c->anc=u;
				v->left->next->next=a;
				a->anc=v;
				a->next=NULL;
				a->dlen=y;
				u->dlen=x-y;
				c->dlen=mprime-x;
				TraceDirtynessToRoot(c->anc);
	//			tree->AdjustCLArrayFlagsBelow(c->anc, curMove);
				}
			//case 2
			else{//u and v physically swap positions in this case
				v->left=a;
				a->anc=v;
				a->next=d;
				d->next=NULL;
				u->left=v;
				u->next=v->next;
				v->next=b;
				b->next=NULL;
				u->anc=c;
				v->anc=u;
				c->left=u;
				a->dlen=y;
				v->dlen=x-y;
				u->dlen=mprime-x;
				TraceDirtynessToRoot(a->anc);
//				tree->AdjustCLArrayFlagsBelow(a->anc, curMove);
				}
			}
		}
	}

void Tree::NNIMutate(int node, int branch, FLOAT_TYPE optPrecision, int subtreeNode){

	assert(0);
	TreeNode* connector=NULL;
	TreeNode* cut=NULL;
	TreeNode* broken=NULL;
	TreeNode* sib=NULL;

	assert(node<allNodes.size());
	connector = allNodes[node];
	assert(connector->IsInternal());

	if(branch==0){
		cut=connector->left;
		sib=connector->right;
		}
	else{
		cut=connector->right;
		sib=connector->left;
		}

	SweepDirtynessOverTree(cut);

	//cut will be attached to connector's next or prev
	if(connector->next!=NULL) broken=connector->next;
	else{
		if(connector->anc==root){
			//special case if connector's anc is root and connector is the rightmost decendent
			broken=connector->prev->prev;
			}
		else broken=connector->prev;
		}

	//take out connector and substitute cut's sib for it
   	connector->SubstituteNodeWithRespectToAnc(sib);

	//establish correct topology for connector and cut nodes
	connector->left=connector->right=cut;
	connector->next=connector->prev=connector->anc=cut->next=cut->prev=NULL;

	//assign branchlengths such that the previous blen of broken is divided between
	//broken and connector
	//cut will keep its original blen.  Connector's old blen will be added to sib
	sib->dlen+=connector->dlen;

	if(broken->dlen*.5 > min_brlen){
		connector->dlen=broken->dlen*(FLOAT_TYPE).5;
		broken->dlen-=connector->dlen;
		}
	else connector->dlen=broken->dlen=min_brlen;

	//put everything in its place
	broken->SubstituteNodeWithRespectToAnc(connector);
	connector->AddDes(broken);

	//try some branch length optimization
	SweepDirtynessOverTree(connector, cut);
	MakeNodeDirty(connector);

#ifdef OPT_DEBUG
	opt << "NNI\n";
	optsum << "NNI\n";
#endif

	OptimizeBranchesWithinRadius(connector, optPrecision, subtreeNode, NULL);
	}

/*
void Tree::OutputBinaryFormattedTree(ofstream &out) const{

	for(int i=0;i<allNodes.size();i++){
		allNodes[i]->OutputBinaryNodeInfo(out);
		}
	out.write((char*) &lnL, sizeof(FLOAT_TYPE));
	out.write((char*) &numTipsTotal, sizeof(numTipsTotal));
	out.write((char*) &numTipsAdded, sizeof(numTipsAdded));
	out.write((char*) &numNodesAdded, sizeof(numNodesAdded));
	out.write((char*) &numBranchesAdded, sizeof(numBranchesAdded));
	}
*/

void Tree::OutputBinaryFormattedTree(OUTPUT_CLASS &out) const{

	out.WRITE_TO_FILE(&numTipsTotal, sizeof(numTipsTotal), 1);
	out.WRITE_TO_FILE(&lnL, sizeof(FLOAT_TYPE), 1);
	out.WRITE_TO_FILE(&numTipsAdded, sizeof(numTipsAdded), 1);
	out.WRITE_TO_FILE(&numNodesAdded, sizeof(numNodesAdded), 1);
	out.WRITE_TO_FILE(&numBranchesAdded, sizeof(numBranchesAdded), 1);
	int numNodesTotal = allNodes.size();
	out.WRITE_TO_FILE(&numNodesTotal, sizeof(numNodesTotal), 1);

	for(int i=0;i<allNodes.size();i++){
		allNodes[i]->OutputBinaryNodeInfo(out);
		}
	}

void Tree::ReadBinaryFormattedTree(FILE *in){
	//this allows a check that the checkpoint was written for the same
	//dataset that was specified in the conf
	int expectedNumTipsTotal = numTipsTotal;

	fread((char*) &numTipsTotal, sizeof(numTipsTotal), 1, in);
	if(numTipsTotal != expectedNumTipsTotal){
		int wrong = numTipsTotal;
		numTipsTotal = expectedNumTipsTotal;
		throw ErrorException("Number of taxa from checkpoint (%d) is not the same as in the current\n\tdatafile (%d)! The checkpoint seems to be from a different run!", wrong, expectedNumTipsTotal);
		}

	fread((char*) &lnL, sizeof(FLOAT_TYPE), 1, in);
	fread((char*) &numTipsAdded, sizeof(numTipsAdded), 1, in);
	fread((char*) &numNodesAdded, sizeof(numNodesAdded), 1, in);
	fread((char*) &numBranchesAdded, sizeof(numBranchesAdded), 1, in);
	int numNodesTotal;
	fread((char*) &numNodesTotal, sizeof(numNodesTotal), 1, in);
	int dum;

	fread((char*) &dum, sizeof(dum), 1, in);
	allNodes[0]->left = allNodes[dum];

	fread((char*) &dum, sizeof(dum), 1, in);
	allNodes[0]->right = allNodes[dum];

	fread((char*) &dum, sizeof(dum), 1, in);
	if(dum == 0) allNodes[0]->prev = NULL;
	else allNodes[0]->prev = allNodes[dum];

	fread((char*) &dum, sizeof(dum), 1, in);
	if(dum == 0) allNodes[0]->next = NULL;
	else allNodes[0]->next = allNodes[dum];

	fread((char*) &dum, sizeof(dum), 1, in);
	if(dum == 0) allNodes[0]->anc = NULL;
	else allNodes[0]->anc = allNodes[dum];

	fread((char*) &allNodes[0]->dlen, sizeof(FLOAT_TYPE), 1, in);

//	double d;
	for(int i=1;i<=numTipsTotal;i++){
		fread(&dum, sizeof(dum), 1, in);
		if(dum == 0) allNodes[i]->prev = NULL;
		else allNodes[i]->prev = allNodes[dum];

		fread(&dum, sizeof(dum), 1, in);
		if(dum == 0) allNodes[i]->next = NULL;
		else allNodes[i]->next = allNodes[dum];

		//all non-root nodes will have an anc, which might be nodenum 0 (the root)
		//so, don't test for zero here
		fread(&dum, sizeof(dum), 1, in);
		allNodes[i]->anc = allNodes[dum];

		fread(&(allNodes[i]->dlen), sizeof(FLOAT_TYPE), 1, in);
		}

	for(int i=numTipsTotal+1;i<allNodes.size();i++){
		fread((char*) &dum, sizeof(dum), 1, in);
		allNodes[i]->left = allNodes[dum];

		fread((char*) &dum, sizeof(dum), 1, in);
		allNodes[i]->right = allNodes[dum];

		fread((char*) &dum, sizeof(dum), 1, in);
		if(dum == 0) allNodes[i]->prev = NULL;
		else allNodes[i]->prev = allNodes[dum];

		fread((char*) &dum, sizeof(dum), 1, in);
		if(dum == 0) allNodes[i]->next = NULL;
		else allNodes[i]->next = allNodes[dum];

		//all non-root nodes will have an anc, which might be nodenum 0 (the root)
		//so, don't test for zero here
		fread((char*) &dum, sizeof(dum), 1, in);
		allNodes[i]->anc = allNodes[dum];

		fread((char*) &allNodes[i]->dlen, sizeof(FLOAT_TYPE), 1, in);
		}
	}

FLOAT_TYPE Tree::OptimizeOmegaParameters(FLOAT_TYPE prec){
	FLOAT_TYPE omegaImprove=ZERO_POINT_ZERO;
	FLOAT_TYPE minVal = 1.0e-5;
	int i=0;

#undef DEBUG_OMEGA_OPT

	//limiting change in any one pass
	double maxRateChangeProp = 0.5;
	double maxProbChange = 0.10;

	//DEBUG - limiting here now too
	if(mod->NRateCats() == 1) //allow a bit more change for a single omega
		omegaImprove += OptimizeBoundedParameter(prec, mod->Omega(i), 0,
			max(minVal, mod->Omega(0)*0.333),
			min(9999.9, mod->Omega(0)+mod->Omega(0)*0.333),
			&Model::SetOmega);
	else{
		omegaImprove += OptimizeBoundedParameter(prec, mod->Omega(i), i,
			max(minVal, mod->Omega(i)*maxRateChangeProp),
			min(mod->Omega(i+1), mod->Omega(i)+mod->Omega(i)*maxRateChangeProp),
			&Model::SetOmega);

#ifdef DEBUG_OMEGA_OPT
		for(int j=0;j<mod->NRateCats();j++)
			outman.UserMessage("%f\t%f", mod->Omega(j), mod->OmegaProb(j));
#endif

		omegaImprove += OptimizeBoundedParameter(prec, mod->OmegaProb(i), i,
			max(minVal, mod->OmegaProb(i)-maxProbChange),
			min(ONE_POINT_ZERO,  mod->OmegaProb(i)+maxProbChange),
			&Model::SetOmegaProb);

#ifdef DEBUG_OMEGA_OPT
		for(int j=0;j<mod->NRateCats();j++)
			outman.UserMessage("%f\t%f", mod->Omega(j), mod->OmegaProb(j));
#endif

		for(i=1;i < mod->NRateCats()-1;i++){
			omegaImprove += OptimizeBoundedParameter(prec, mod->Omega(i), i,
				max(mod->Omega(i-1), mod->Omega(i)*maxRateChangeProp),
				min(mod->Omega(i+1), mod->Omega(i)+mod->Omega(i)*maxRateChangeProp),
				&Model::SetOmega);

#ifdef DEBUG_OMEGA_OPT
			for(int j=0;j<mod->NRateCats();j++)
				outman.UserMessage("%f\t%f", mod->Omega(j), mod->OmegaProb(j));
#endif

			omegaImprove += OptimizeBoundedParameter(prec, mod->OmegaProb(i), i,
				max(minVal, mod->OmegaProb(i)-maxProbChange),
				min(ONE_POINT_ZERO,  mod->OmegaProb(i)+maxProbChange),
				&Model::SetOmegaProb);

#ifdef DEBUG_OMEGA_OPT
			for(int j=0;j<mod->NRateCats();j++)
				outman.UserMessage("%f\t%f", mod->Omega(j), mod->OmegaProb(j));
#endif
			}
		omegaImprove += OptimizeBoundedParameter(prec, mod->Omega(i), i,
			max(mod->Omega(i-1), mod->Omega(i)*maxRateChangeProp),
			min(9999.9, mod->Omega(i)+mod->Omega(i)*maxRateChangeProp),
			&Model::SetOmega);

#ifdef DEBUG_OMEGA_OPT
		for(int j=0;j<mod->NRateCats();j++)
			outman.UserMessage("%f\t%f", mod->Omega(j), mod->OmegaProb(j));
#endif

		omegaImprove += OptimizeBoundedParameter(prec, mod->OmegaProb(i), i,
			max(minVal, mod->OmegaProb(i)-maxProbChange),
			min(ONE_POINT_ZERO,  mod->OmegaProb(i)+maxProbChange),
			&Model::SetOmegaProb);

#ifdef DEBUG_OMEGA_OPT
		for(int j=0;j<mod->NRateCats();j++)
			outman.UserMessage("%f\t%f", mod->Omega(j), mod->OmegaProb(j));
#endif
		}

/*
	if(mod->NRateCats() == 1)
		omegaImprove += OptimizeBoundedParameter(prec, mod->Omega(i), 0, minVal, 9999.9, &Model::SetOmega);
	else{
		omegaImprove += OptimizeBoundedParameter(prec, mod->Omega(i), i, minVal, mod->Omega(1), &Model::SetOmega);
//		for(int j=0;j<mod->NRateCats();j++)
//			cout << mod->Omega(j) << "\t" << mod->OmegaProb(j) << endl;
		omegaImprove += OptimizeBoundedParameter(prec, mod->OmegaProb(i), i, minVal, ONE_POINT_ZERO, &Model::SetOmegaProb);
//		for(int j=0;j<mod->NRateCats();j++)
//			cout << mod->Omega(j) << "\t" << mod->OmegaProb(j) << endl;
		for(i=1;i < mod->NRateCats()-1;i++){
			omegaImprove += OptimizeBoundedParameter(prec, mod->Omega(i), i, mod->Omega(i-1), mod->Omega(i+1), &Model::SetOmega);
//			for(int j=0;j<mod->NRateCats();j++)
//				cout << mod->Omega(j) << "\t" << mod->OmegaProb(j) << endl;
			omegaImprove += OptimizeBoundedParameter(prec, mod->OmegaProb(i), i, minVal, ONE_POINT_ZERO, &Model::SetOmegaProb);
//			for(int j=0;j<mod->NRateCats();j++)
//				cout << mod->Omega(j) << "\t" << mod->OmegaProb(j) << endl;
			}
		omegaImprove += OptimizeBoundedParameter(prec, mod->Omega(i), i, mod->Omega(i-1), 9999.9, &Model::SetOmega);
//		for(int j=0;j<mod->NRateCats();j++)
//			cout << mod->Omega(j) << "\t" << mod->OmegaProb(j) << endl;
		omegaImprove += OptimizeBoundedParameter(prec, mod->OmegaProb(i), i, minVal, ONE_POINT_ZERO, &Model::SetOmegaProb);
//		for(int j=0;j<mod->NRateCats();j++)
//			cout << mod->Omega(j) << "\t" << mod->OmegaProb(j) << endl;
		}
*/	return omegaImprove;
	}

FLOAT_TYPE Tree::OptimizeFlexRates(FLOAT_TYPE prec){
	FLOAT_TYPE flexImprove=ZERO_POINT_ZERO;
	FLOAT_TYPE minVal = 1.0e-5;
	int i=0;

	//limiting change in any one pass
	double maxRateChangeProp = 0.75;
	double maxProbChange = 0.20;

	flexImprove += OptimizeBoundedParameter(prec, mod->FlexRate(i), i,
		max(minVal, mod->FlexRate(i)*maxRateChangeProp),
		min(mod->FlexRate(i+1), mod->FlexRate(i)+mod->FlexRate(i)*maxRateChangeProp),
		&Model::SetFlexRate);

	flexImprove += OptimizeBoundedParameter(prec, mod->FlexProb(i), i,
		max(minVal, mod->FlexProb(i)-maxProbChange),
		min(ONE_POINT_ZERO, mod->FlexProb(i)+maxProbChange),
		&Model::SetFlexProb);
	for(i=1;i < mod->NRateCats()-1;i++){
		flexImprove += OptimizeBoundedParameter(prec, mod->FlexRate(i), i,
			max(mod->FlexRate(i-1), mod->FlexRate(i)*maxRateChangeProp),
			min(mod->FlexRate(i+1), mod->FlexRate(i)+mod->FlexRate(i)*maxRateChangeProp),
			&Model::SetFlexRate);
		flexImprove += OptimizeBoundedParameter(prec, mod->FlexProb(i), i,
			max(minVal, mod->FlexProb(i)-maxProbChange),
			min(ONE_POINT_ZERO, mod->FlexProb(i)+maxProbChange),
			&Model::SetFlexProb);
		}
	flexImprove += OptimizeBoundedParameter(prec, mod->FlexRate(i), i,
		max(mod->FlexRate(i-1), mod->FlexRate(i)*maxRateChangeProp),
		min(999.9, mod->FlexRate(i)+mod->FlexRate(i)*maxRateChangeProp),
		&Model::SetFlexRate);
	flexImprove += OptimizeBoundedParameter(prec, mod->FlexProb(i), i,
		max(minVal, mod->FlexProb(i)-maxProbChange),
		min(ONE_POINT_ZERO, mod->FlexProb(i)+maxProbChange),
		&Model::SetFlexProb);


/*
	flexImprove += OptimizeBoundedParameter(prec, mod->FlexRate(i), i, minVal, mod->FlexRate(i+1), &Model::SetFlexRate);

//		for(int j=0;j<mod->NRateCats();j++)
//			cout << mod->FlexRate(j) << "\t" << mod->FlexProb(j) << endl;
	flexImprove += OptimizeBoundedParameter(prec, mod->FlexProb(i), i, minVal, ONE_POINT_ZERO, &Model::SetFlexProb);
//		for(int j=0;j<mod->NRateCats();j++)
//			cout << mod->FlexRate(j) << "\t" << mod->FlexProb(j) << endl;
	for(i=1;i < mod->NRateCats()-1;i++){
		flexImprove += OptimizeBoundedParameter(prec, mod->FlexRate(i), i, mod->FlexRate(i-1), mod->FlexRate(i+1), &Model::SetFlexRate);
//			for(int j=0;j<mod->NRateCats();j++)
//				cout << mod->FlexRate(j) << "\t" << mod->FlexProb(j) << endl;
		flexImprove += OptimizeBoundedParameter(prec, mod->FlexProb(i), i, minVal, ONE_POINT_ZERO, &Model::SetFlexProb);
//			for(int j=0;j<mod->NRateCats();j++)
//				cout << mod->FlexRate(j) << "\t" << mod->FlexProb(j) << endl;
		}
	flexImprove += OptimizeBoundedParameter(prec, mod->FlexRate(i), i, mod->FlexRate(i-1), 9999.9, &Model::SetFlexRate);
//		for(int j=0;j<mod->NRateCats();j++)
//			cout << mod->FlexRate(j) << "\t" << mod->FlexProb(j) << endl;
	flexImprove += OptimizeBoundedParameter(prec, mod->FlexProb(i), i, minVal, ONE_POINT_ZERO, &Model::SetFlexProb);
//		for(int j=0;j<mod->NRateCats();j++)
//			cout << mod->FlexRate(j) << "\t" << mod->FlexProb(j) << endl;
*/
	return flexImprove;
	}

void Tree::CalcFullCLAInternalInternalEQUIV(CondLikeArray *destCLA, const CondLikeArray *LCLA, const CondLikeArray *RCLA, const FLOAT_TYPE *Lpr, const FLOAT_TYPE *Rpr, const char *leftEQ, const char *rightEQ){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *dest=destCLA->arr;
	const FLOAT_TYPE *LCL=LCLA->arr;
	const FLOAT_TYPE *RCL=RCLA->arr;
	FLOAT_TYPE L1, L2, L3, L4, R1, R2, R3, R4;

	const int nRateCats = mod->NRateCats();
	const int nchar = data->NChar();
	assert(nRateCats == 1);

#ifdef UNIX
	madvise(dest, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void *)LCL, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void *)RCL, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

	for(int i=0;i<nchar;i++) {
		if(leftEQ[i] == false){
			L1=( Lpr[0]*LCL[0]+Lpr[1]*LCL[1]+Lpr[2]*LCL[2]+Lpr[3]*LCL[3]);
			L2=( Lpr[4]*LCL[0]+Lpr[5]*LCL[1]+Lpr[6]*LCL[2]+Lpr[7]*LCL[3]);
			L3=( Lpr[8]*LCL[0]+Lpr[9]*LCL[1]+Lpr[10]*LCL[2]+Lpr[11]*LCL[3]);
			L4=( Lpr[12]*LCL[0]+Lpr[13]*LCL[1]+Lpr[14]*LCL[2]+Lpr[15]*LCL[3]);
			}

		if(rightEQ[i] == false){
			R1=(Rpr[0]*RCL[0]+Rpr[1]*RCL[1]+Rpr[2]*RCL[2]+Rpr[3]*RCL[3]);
			R2=(Rpr[4]*RCL[0]+Rpr[5]*RCL[1]+Rpr[6]*RCL[2]+Rpr[7]*RCL[3]);
			R3=(Rpr[8]*RCL[0]+Rpr[9]*RCL[1]+Rpr[10]*RCL[2]+Rpr[11]*RCL[3]);
			R4=(Rpr[12]*RCL[0]+Rpr[13]*RCL[1]+Rpr[14]*RCL[2]+Rpr[15]*RCL[3]);
			}

		dest[0] = L1 * R1;
		dest[1] = L2 * R2;
		dest[2] = L3 * R3;
		dest[3] = L4 * R4;

		assert(dest[0] == dest[0]);
		assert(dest[0] >= 0);
		dest += 4;
		LCL += 4;
		RCL += 4;
		}

	const int *left_mult=LCLA->underflow_mult;
	const int *right_mult=RCLA->underflow_mult;
	int *undermult=destCLA->underflow_mult;

	for(int i=0;i<nchar;i++){
		undermult[i] = left_mult[i] + right_mult[i];
		}
	destCLA->rescaleRank = 2 + LCLA->rescaleRank + RCLA->rescaleRank;
	}

void Tree::CalcFullCLAInternalInternal(CondLikeArray *destCLA, const CondLikeArray *LCLA, const CondLikeArray *RCLA, const FLOAT_TYPE *Lpr, const FLOAT_TYPE *Rpr){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *dest=destCLA->arr;
	const FLOAT_TYPE *LCL=LCLA->arr;
	const FLOAT_TYPE *RCL=RCLA->arr;
	FLOAT_TYPE L1, L2, L3, L4, R1, R2, R3, R4;

	const int nRateCats = mod->NRateCats();
	const int nchar = data->NChar();
	const int *counts = data->GetCounts();

#ifdef CUDA_GPU
	if (cudaman->GetGPUCLAEnabled()) {
		cudaman->ComputeGPUCLA(Lpr, Rpr, LCL, RCL, dest);
	} else {
#endif

#ifdef UNIX
	madvise(dest, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void *)LCL, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void *)RCL, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

	if(nRateCats == 4){//the unrolled 4 rate version
#ifdef OMP_INTINTCLA
		#pragma omp parallel for private(dest, LCL, RCL, L1, L2, L3, L4, R1, R2, R3, R4)
		for(int i=0;i<nchar;i++){
			int index=4*4*i;
			dest = &(destCLA->arr[index]);
			LCL = &(LCLA->arr[index]);
			RCL= &(RCLA->arr[index]);
#else
		for(int i=0;i<nchar;i++){
#endif
#ifdef USE_COUNTS_IN_BOOT
			if(counts[i]> 0){
#else
			if(1){
#endif
				L1=((Lpr[0]*LCL[0])+(Lpr[1]*LCL[1]))+((Lpr[2]*LCL[2])+(Lpr[3]*LCL[3]));
				L2=((Lpr[4]*LCL[0])+(Lpr[5]*LCL[1]))+((Lpr[6]*LCL[2])+(Lpr[7]*LCL[3]));
				L3=((Lpr[8]*LCL[0])+(Lpr[9]*LCL[1]))+((Lpr[10]*LCL[2])+(Lpr[11]*LCL[3]));
				L4=((Lpr[12]*LCL[0])+(Lpr[13]*LCL[1]))+((Lpr[14]*LCL[2])+(Lpr[15]*LCL[3]));

				R1=((Rpr[0]*RCL[0])+(Rpr[1]*RCL[1]))+((Rpr[2]*RCL[2])+(Rpr[3]*RCL[3]));
				R2=((Rpr[4]*RCL[0])+(Rpr[5]*RCL[1]))+((Rpr[6]*RCL[2])+(Rpr[7]*RCL[3]));
				R3=((Rpr[8]*RCL[0])+(Rpr[9]*RCL[1]))+((Rpr[10]*RCL[2])+(Rpr[11]*RCL[3]));
				R4=((Rpr[12]*RCL[0])+(Rpr[13]*RCL[1]))+((Rpr[14]*RCL[2])+(Rpr[15]*RCL[3]));

				dest[0] = L1 * R1;
				dest[1] = L2 * R2;
				dest[2] = L3 * R3;
				dest[3] = L4 * R4;

				dest+=4;
				LCL+=4;
				RCL+=4;

				L1=(Lpr[16+0]*LCL[0]+Lpr[16+1]*LCL[1])+(Lpr[16+2]*LCL[2]+Lpr[16+3]*LCL[3]);
				L2=(Lpr[16+4]*LCL[0]+Lpr[16+5]*LCL[1])+(Lpr[16+6]*LCL[2]+Lpr[16+7]*LCL[3]);
				L3=(Lpr[16+8]*LCL[0]+Lpr[16+9]*LCL[1])+(Lpr[16+10]*LCL[2]+Lpr[16+11]*LCL[3]);
				L4=(Lpr[16+12]*LCL[0]+Lpr[16+13]*LCL[1])+(Lpr[16+14]*LCL[2]+Lpr[16+15]*LCL[3]);

				R1=(Rpr[16+0]*RCL[0]+Rpr[16+1]*RCL[1])+(Rpr[16+2]*RCL[2]+Rpr[16+3]*RCL[3]);
				R2=(Rpr[16+4]*RCL[0]+Rpr[16+5]*RCL[1])+(Rpr[16+6]*RCL[2]+Rpr[16+7]*RCL[3]);
				R3=(Rpr[16+8]*RCL[0]+Rpr[16+9]*RCL[1])+(Rpr[16+10]*RCL[2]+Rpr[16+11]*RCL[3]);
				R4=(Rpr[16+12]*RCL[0]+Rpr[16+13]*RCL[1])+(Rpr[16+14]*RCL[2]+Rpr[16+15]*RCL[3]);

				dest[0] = L1 * R1;
				dest[1] = L2 * R2;
				dest[2] = L3 * R3;
				dest[3] = L4 * R4;

				dest+=4;
				LCL+=4;
				RCL+=4;

				L1=(Lpr[32+0]*LCL[0]+Lpr[32+1]*LCL[1])+(Lpr[32+2]*LCL[2]+Lpr[32+3]*LCL[3]);
				L2=(Lpr[32+4]*LCL[0]+Lpr[32+5]*LCL[1])+(Lpr[32+6]*LCL[2]+Lpr[32+7]*LCL[3]);
				L3=(Lpr[32+8]*LCL[0]+Lpr[32+9]*LCL[1])+(Lpr[32+10]*LCL[2]+Lpr[32+11]*LCL[3]);
				L4=(Lpr[32+12]*LCL[0]+Lpr[32+13]*LCL[1])+(Lpr[32+14]*LCL[2]+Lpr[32+15]*LCL[3]);

				R1=(Rpr[32+0]*RCL[0]+Rpr[32+1]*RCL[1])+(Rpr[32+2]*RCL[2]+Rpr[32+3]*RCL[3]);
				R2=(Rpr[32+4]*RCL[0]+Rpr[32+5]*RCL[1])+(Rpr[32+6]*RCL[2]+Rpr[32+7]*RCL[3]);
				R3=(Rpr[32+8]*RCL[0]+Rpr[32+9]*RCL[1])+(Rpr[32+10]*RCL[2]+Rpr[32+11]*RCL[3]);
				R4=(Rpr[32+12]*RCL[0]+Rpr[32+13]*RCL[1])+(Rpr[32+14]*RCL[2]+Rpr[32+15]*RCL[3]);

				dest[0] = L1 * R1;
				dest[1] = L2 * R2;
				dest[2] = L3 * R3;
				dest[3] = L4 * R4;

				dest+=4;
				LCL+=4;
				RCL+=4;

				L1=(Lpr[48+0]*LCL[0]+Lpr[48+1]*LCL[1])+(Lpr[48+2]*LCL[2]+Lpr[48+3]*LCL[3]);
				L2=(Lpr[48+4]*LCL[0]+Lpr[48+5]*LCL[1])+(Lpr[48+6]*LCL[2]+Lpr[48+7]*LCL[3]);
				L3=(Lpr[48+8]*LCL[0]+Lpr[48+9]*LCL[1])+(Lpr[48+10]*LCL[2]+Lpr[48+11]*LCL[3]);
				L4=(Lpr[48+12]*LCL[0]+Lpr[48+13]*LCL[1])+(Lpr[48+14]*LCL[2]+Lpr[48+15]*LCL[3]);

				R1=(Rpr[48+0]*RCL[0]+Rpr[48+1]*RCL[1])+(Rpr[48+2]*RCL[2]+Rpr[48+3]*RCL[3]);
				R2=(Rpr[48+4]*RCL[0]+Rpr[48+5]*RCL[1])+(Rpr[48+6]*RCL[2]+Rpr[48+7]*RCL[3]);
				R3=(Rpr[48+8]*RCL[0]+Rpr[48+9]*RCL[1])+(Rpr[48+10]*RCL[2]+Rpr[48+11]*RCL[3]);
				R4=(Rpr[48+12]*RCL[0]+Rpr[48+13]*RCL[1])+(Rpr[48+14]*RCL[2]+Rpr[48+15]*RCL[3]);

				dest[0] = L1 * R1;
				dest[1] = L2 * R2;
				dest[2] = L3 * R3;
				dest[3] = L4 * R4;

				dest+=4;
				LCL+=4;
				RCL+=4;

#ifdef ALLOW_SINGLE_SITE
				if(siteToScore > -1) break;
#endif
				}
			}
		}

	else{//the general N rate version
		int r;
#ifdef OMP_INTINTCLA
		int index;
		#pragma omp parallel for private(r, index, dest, LCL, RCL, L1, L2, L3, L4, R1, R2, R3, R4)
		for(int i=0;i<nchar;i++) {
			index=4*nRateCats*i;
			dest = &(destCLA->arr[index]);
			LCL = &(LCLA->arr[index]);
			RCL= &(RCLA->arr[index]);
#else
		for(int i=0;i<nchar;i++) {
#endif
#ifdef USE_COUNTS_IN_BOOT
			if(counts[i] > 0){
#else
			if(1){
#endif
				for(r=0;r<nRateCats;r++){
					L1=( Lpr[16*r+0]*LCL[0]+Lpr[16*r+1]*LCL[1]+Lpr[16*r+2]*LCL[2]+Lpr[16*r+3]*LCL[3]);
					L2=( Lpr[16*r+4]*LCL[0]+Lpr[16*r+5]*LCL[1]+Lpr[16*r+6]*LCL[2]+Lpr[16*r+7]*LCL[3]);
					L3=( Lpr[16*r+8]*LCL[0]+Lpr[16*r+9]*LCL[1]+Lpr[16*r+10]*LCL[2]+Lpr[16*r+11]*LCL[3]);
					L4=( Lpr[16*r+12]*LCL[0]+Lpr[16*r+13]*LCL[1]+Lpr[16*r+14]*LCL[2]+Lpr[16*r+15]*LCL[3]);

					R1=(Rpr[16*r+0]*RCL[0]+Rpr[16*r+1]*RCL[1]+Rpr[16*r+2]*RCL[2]+Rpr[16*r+3]*RCL[3]);
					R2=(Rpr[16*r+4]*RCL[0]+Rpr[16*r+5]*RCL[1]+Rpr[16*r+6]*RCL[2]+Rpr[16*r+7]*RCL[3]);
					R3=(Rpr[16*r+8]*RCL[0]+Rpr[16*r+9]*RCL[1]+Rpr[16*r+10]*RCL[2]+Rpr[16*r+11]*RCL[3]);
					R4=(Rpr[16*r+12]*RCL[0]+Rpr[16*r+13]*RCL[1]+Rpr[16*r+14]*RCL[2]+Rpr[16*r+15]*RCL[3]);

					dest[0] = L1 * R1;
					dest[1] = L2 * R2;
					dest[2] = L3 * R3;
					dest[3] = L4 * R4;

					dest+=4;
					LCL+=4;
					RCL+=4;
					}
#ifdef ALLOW_SINGLE_SITE
				if(siteToScore > -1) break;
#endif
				}
			}
		}

#ifdef CUDA_GPU
	}
#endif

	const int *left_mult=LCLA->underflow_mult;
	const int *right_mult=RCLA->underflow_mult;
	int *undermult=destCLA->underflow_mult;

	for(int i=0;i<nchar;i++){
		undermult[i] = left_mult[i] + right_mult[i];
		}
	destCLA->rescaleRank = 2 + LCLA->rescaleRank + RCLA->rescaleRank;
	}

void Tree::CalcFullCLAInternalInternalNState(CondLikeArray *destCLA, const CondLikeArray *LCLA, const CondLikeArray *RCLA, const FLOAT_TYPE *Lpr, const FLOAT_TYPE *Rpr){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *dest=destCLA->arr;
	const FLOAT_TYPE *LCL=LCLA->arr;
	const FLOAT_TYPE *RCL=RCLA->arr;
	FLOAT_TYPE L1, R1;

	const int nRateCats = mod->NRateCats();
	const int nstates = mod->NStates();
	const int nchar = data->NChar();
	const int *counts = data->GetCounts();

#ifdef CUDA_GPU
	if (cudaman->GetGPUCLAEnabled()) {
		cudaman->ComputeGPUCLA(Lpr, Rpr, LCL, RCL, dest);
	} else {
#endif

#ifdef UNIX
	madvise(dest, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void *)LCL, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void *)RCL, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

#ifdef OMP_INTINTCLA_NSTATE
	#pragma omp parallel for private(dest, LCL, RCL, L1, R1)
	for(int i=0;i<nchar;i++){
		dest = &(destCLA->arr[nRateCats * nstates * i]);
		LCL = &(LCLA->arr[nRateCats * nstates * i]);
		RCL= &(RCLA->arr[nRateCats * nstates * i]);

#else
	for(int i=0;i<nchar;i++) {
#endif
#ifdef USE_COUNTS_IN_BOOT
		if(counts[i]> 0){
#else
		if(1)
#endif
			for(int rate=0;rate<nRateCats;rate++){
				for(int from=0;from<nstates;from++){
					L1 = R1 = ZERO_POINT_ZERO;
					for(int to=0;to<nstates;to++){
						L1 += Lpr[rate*nstates*nstates + from*nstates + to] * LCL[to];
						R1 += Rpr[rate*nstates*nstates + from*nstates + to] * RCL[to];
						}
					dest[from] = L1 * R1;
					}
				LCL += nstates;
				RCL += nstates;
				dest += nstates;
				}
			assert(dest[-nstates*nRateCats] >= 0.0);
			assert(dest[-nstates*nRateCats] == dest[-nstates*nRateCats]);

#ifdef ALLOW_SINGLE_SITE
				if(siteToScore > -1) break;
#endif
			}
		}

#ifdef CUDA_GPU
	}
#endif

	const int *left_mult=LCLA->underflow_mult;
	const int *right_mult=RCLA->underflow_mult;
	int *undermult=destCLA->underflow_mult;

	for(int i=0;i<nchar;i++){
		undermult[i] = left_mult[i] + right_mult[i];
		}
	destCLA->rescaleRank = 2 + LCLA->rescaleRank + RCLA->rescaleRank;
	}

void Tree::CalcFullCLATerminalTerminal(CondLikeArray *destCLA, const FLOAT_TYPE *Lpr, const FLOAT_TYPE *Rpr, const char *Ldata, const char *Rdata){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *dest=destCLA->arr;

	const int nRateCats = mod->NRateCats();
	const int nchar = data->NChar();
	const int *counts = data->GetCounts();

#ifdef UNIX
	madvise(dest, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

#ifdef ALLOW_SINGLE_SITE
	if(siteToScore > 0){
		Ldata = AdvanceDataPointer(Ldata, siteToScore);
		Rdata = AdvanceDataPointer(Rdata, siteToScore);
		}
#endif

	for(int i=0;i<nchar;i++){
#ifdef USE_COUNTS_IN_BOOT
		if(counts[i] > 0){
#else
		if(1){
#endif
			if(*Ldata > -1 && *Rdata > -1){
				for(int r=0;r<nRateCats;r++){
					*(dest++) = Lpr[(*Ldata)+16*r] * Rpr[(*Rdata)+16*r];
					*(dest++) = Lpr[(*Ldata+4)+16*r] * Rpr[(*Rdata+4)+16*r];
					*(dest++) = Lpr[(*Ldata+8)+16*r] * Rpr[(*Rdata+8)+16*r];
					*(dest++) = Lpr[(*Ldata+12)+16*r] * Rpr[(*Rdata+12)+16*r];
					}
				Ldata++;
				Rdata++;
				}

			else if((*Ldata == -4 && *Rdata == -4) || (*Ldata == -4 && *Rdata > -1) || (*Rdata == -4 && *Ldata > -1)){//total ambiguity of left, right or both

				if(*Ldata == -4 && *Rdata == -4) //total ambiguity of both
					for(int i=0;i< (4*nRateCats);i++) *(dest++) = ONE_POINT_ZERO;

				else if(*Ldata == -4){//total ambiguity of left
					for(int i=0;i<nRateCats;i++){
						*(dest++) = Rpr[(*Rdata)+16*i];
						*(dest++) = Rpr[(*Rdata+4)+16*i];
						*(dest++) = Rpr[(*Rdata+8)+16*i];
						*(dest++) = Rpr[(*Rdata+12)+16*i];
						assert(*(dest-4)>=ZERO_POINT_ZERO);
						}
					}
				else{//total ambiguity of right
					for(int i=0;i<nRateCats;i++){
						*(dest++) = Lpr[(*Ldata)+16*i];
						*(dest++) = Lpr[(*Ldata+4)+16*i];
						*(dest++) = Lpr[(*Ldata+8)+16*i];
						*(dest++) = Lpr[(*Ldata+12)+16*i];
						assert(*(dest-4)>=ZERO_POINT_ZERO);
						}
					}
				Ldata++;
				Rdata++;
				}
			else {//partial ambiguity of left, right or both
				if(*Ldata>-1){//unambiguous left
					for(int i=0;i<nRateCats;i++){
						*(dest+(i*4)) = Lpr[(*Ldata)+16*i];
						*(dest+(i*4)+1) = Lpr[(*Ldata+4)+16*i];
						*(dest+(i*4)+2) = Lpr[(*Ldata+8)+16*i];
						*(dest+(i*4)+3) = Lpr[(*Ldata+12)+16*i];
						assert(*(dest)>=ZERO_POINT_ZERO);
						}
					Ldata++;
					}
				else{
					if(*Ldata==-4){//fully ambiguous left
						for(int i=0;i< (4*nRateCats);i++){
							*(dest+i)=ONE_POINT_ZERO;
							}
						Ldata++;
						}

					else{//partially ambiguous left
						int nstates=-*(Ldata++);
						for(int q=0;q< (4*nRateCats);q++) dest[q]=0;
						for(int i=0;i<nstates;i++){
							for(int r=0;r<nRateCats;r++){
								*(dest+(r*4)) += Lpr[(*Ldata)+16*r];
								*(dest+(r*4)+1) += Lpr[(*Ldata+4)+16*r];
								*(dest+(r*4)+2) += Lpr[(*Ldata+8)+16*r];
								*(dest+(r*4)+3) += Lpr[(*Ldata+12)+16*r];
								}
							Ldata++;
							}
						}
					}
				if(*Rdata>-1){//unambiguous right
					for(int i=0;i<nRateCats;i++){
						*(dest++) *= Rpr[(*Rdata)+16*i];
						*(dest++) *= Rpr[(*Rdata+4)+16*i];
						*(dest++) *= Rpr[(*Rdata+8)+16*i];
						*(dest++) *= Rpr[(*Rdata+12)+16*i];
						}
					Rdata++;
					}
				else if(*Rdata != -4){//partially ambiguous right
					char nstates=-1 * *(Rdata++);
					//create a temporary cla to hold the results from the ambiguity of the right,
					//which need to be +'s
					//FLOAT_TYPE *tempcla=new FLOAT_TYPE[4*nRateCats];
					vector<FLOAT_TYPE> tempcla(4*nRateCats);
					for(int i=0;i<nstates;i++){
						for(int r=0;r<nRateCats;r++){
							tempcla[(r*4)]   += Rpr[(*Rdata)+16*r];
							tempcla[(r*4)+1] += Rpr[(*Rdata+4)+16*r];
							tempcla[(r*4)+2] += Rpr[(*Rdata+8)+16*r];
							tempcla[(r*4)+3] += Rpr[(*Rdata+12)+16*r];
							}
						Rdata++;
						}
					//Now multiply the temporary results against the already calced left
					for(int i=0;i<nRateCats;i++){
						*(dest++) *= tempcla[(i*4)];
						*(dest++) *= tempcla[(i*4)+1];
						*(dest++) *= tempcla[(i*4)+2];
						*(dest++) *= tempcla[(i*4)+3];
						}
					}
				else{//fully ambiguous right
					dest+=(4*nRateCats);
					Rdata++;
					}
				}
#ifdef ALLOW_SINGLE_SITE
			if(siteToScore > -1) break;
#endif
			}
		else{//if the count for this site is 0
#ifdef OPEN_MP
			//this is a little strange, but dest only needs to be advanced in the case of OMP
			//because sections of the CLAs corresponding to sites with count=0 are skipped
			//over in OMP instead of being eliminated
			dest += 4 * nRateCats;
#endif
			if(*Ldata > -1 || *Ldata == -4) Ldata++;
			else{
				int states = -1 * *Ldata;
				do{
					Ldata++;
					}while (states-- > 0);
				}
			if(*Rdata > -1 || *Rdata == -4) Rdata++;
			else{
				int states = -1 * *Rdata;
				do{
					Rdata++;
					}while (states-- > 0);
				}
			}
		}

		for(int site=0;site<nchar;site++){
			destCLA->underflow_mult[site]=0;
			}
		destCLA->rescaleRank=2;
	}

void Tree::CalcFullCLATerminalTerminalNState(CondLikeArray *destCLA, const FLOAT_TYPE *Lpr, const FLOAT_TYPE *Rpr, const char *Ldata, const char *Rdata){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *dest=destCLA->arr;

	const int nRateCats = mod->NRateCats();
	const int nstates = mod->NStates();
	const int nchar = data->NChar();
	const int *counts = data->GetCounts();

#ifdef UNIX
	madvise(dest, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif
	if(siteToScore > 0){
		Ldata += siteToScore;
		Rdata += siteToScore;
		}

	for(int i=0;i<nchar;i++){
#ifdef USE_COUNTS_IN_BOOT
		if(counts[i]> 0){
#else
		if(1){
#endif
			if(*Ldata < nstates && *Rdata < nstates){
				for(int rate=0;rate<nRateCats;rate++){
					for(int from=0;from<nstates;from++){
						dest[rate*nstates + from] = Lpr[(*Ldata) + from*nstates + rate*nstates*nstates] * Rpr[(*Rdata) + from*nstates + rate*nstates*nstates];
						}
					}
				Ldata++;
				Rdata++;
				}

			else{//total ambiguity of left, right or both

				if(*Ldata == nstates && *Rdata == nstates) //total ambiguity of both
					for(int rate=0;rate<nRateCats;rate++)
						for(int from=0;from<nstates;from++)
							dest[rate*nstates + from] = ONE_POINT_ZERO;

				else if(*Ldata == nstates){//total ambiguity of left
					for(int rate=0;rate<nRateCats;rate++)
						for(int from=0;from<nstates;from++)
							dest[rate*nstates + from] = Rpr[(*Rdata) + from*nstates + rate*nstates*nstates];

					}
				else{//total ambiguity of right
					for(int rate=0;rate<nRateCats;rate++)
						for(int from=0;from<nstates;from++)
							dest[rate*nstates + from] = Lpr[(*Ldata) + from*nstates + rate*nstates*nstates];
					}
				Ldata++;
				Rdata++;
				}
			dest += nRateCats*nstates;
#ifdef ALLOW_SINGLE_SITE
			if(siteToScore > -1) break;
#endif
			}
		else{
#ifdef OPEN_MP
			//this is a little strange, but dest only needs to be advanced in the case of OMP
			//because sections of the CLAs corresponding to sites with count=0 are skipped
			//over in OMP instead of being eliminated
			dest += nRateCats*nstates;
#endif
			Ldata++;
			Rdata++;
			}
		}
		for(int site=0;site<nchar;site++){
			destCLA->underflow_mult[site]=0;
			}
		destCLA->rescaleRank=2;
	}


void Tree::CalcFullCLAInternalTerminal(CondLikeArray *destCLA, const CondLikeArray *LCLA, const FLOAT_TYPE *pr1, const FLOAT_TYPE *pr2, char *dat2, const unsigned *ambigMap){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *des=destCLA->arr;
	FLOAT_TYPE *dest=des;
	const FLOAT_TYPE *CL=LCLA->arr;
	const FLOAT_TYPE *CL1=CL;
	const char *data2=dat2;

	const int nchar = data->NChar();
	const int nRateCats = mod->NRateCats();
	const int *counts = data->GetCounts();

#ifdef UNIX
	madvise(dest, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void*)CL1, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

#ifdef ALLOW_SINGLE_SITE
	if(siteToScore > 0) data2 = AdvanceDataPointer(data2, siteToScore);
#endif

	if(nRateCats==4){//unrolled 4 rate version
#ifdef OMP_INTTERMCLA
		#pragma omp parallel for private(dest, CL1, data2)
		for(int i=0;i<nchar;i++){
			dest=&des[4*4*i];
			CL1=&CL[4*4*i];
			data2=&dat2[ambigMap[i]];
#else
		for(int i=0;i<nchar;i++){
#endif
#ifdef USE_COUNTS_IN_BOOT
			if(counts[i] > 0){
#else
			if(1){
#endif
				if(*data2 > -1){ //no ambiguity
					dest[0] = ((pr1[0]*CL1[0]+pr1[1]*CL1[1])+(pr1[2]*CL1[2]+pr1[3]*CL1[3])) * pr2[*data2];
					dest[1] = ((pr1[4]*CL1[0]+pr1[5]*CL1[1])+(pr1[6]*CL1[2]+pr1[7]*CL1[3])) * pr2[*data2+4];
					dest[2] = ((pr1[8]*CL1[0]+pr1[9]*CL1[1])+(pr1[10]*CL1[2]+pr1[11]*CL1[3])) * pr2[*data2+8];
					dest[3] = ((pr1[12]*CL1[0]+pr1[13]*CL1[1])+(pr1[14]*CL1[2]+pr1[15]*CL1[3])) * pr2[*data2+12];

					dest[4] = ((pr1[16]*CL1[4]+pr1[17]*CL1[5])+(pr1[18]*CL1[6]+pr1[19]*CL1[7])) * pr2[*data2+16];
					dest[5] = ((pr1[20]*CL1[4]+pr1[21]*CL1[5])+(pr1[22]*CL1[6]+pr1[23]*CL1[7])) * pr2[*data2+4+16];
					dest[6] = ((pr1[24]*CL1[4]+pr1[25]*CL1[5])+(pr1[26]*CL1[6]+pr1[27]*CL1[7])) * pr2[*data2+8+16];
					dest[7] = ((pr1[28]*CL1[4]+pr1[29]*CL1[5])+(pr1[30]*CL1[6]+pr1[31]*CL1[7])) * pr2[*data2+12+16];

					dest[8] = ((pr1[32]*CL1[8]+pr1[33]*CL1[9])+(pr1[34]*CL1[10]+pr1[35]*CL1[11])) * pr2[*data2+32];
					dest[9] = ((pr1[36]*CL1[8]+pr1[37]*CL1[9])+(pr1[38]*CL1[10]+pr1[39]*CL1[11])) * pr2[*data2+4+32];
					dest[10] = ((pr1[40]*CL1[8]+pr1[41]*CL1[9])+(pr1[42]*CL1[10]+pr1[43]*CL1[11])) * pr2[*data2+8+32];
					dest[11] = ((pr1[44]*CL1[8]+pr1[45]*CL1[9])+(pr1[46]*CL1[10]+pr1[47]*CL1[11])) * pr2[*data2+12+32];

					dest[12] = ((pr1[48]*CL1[12]+pr1[49]*CL1[13])+(pr1[50]*CL1[14]+pr1[51]*CL1[15])) * pr2[*data2+48];
					dest[13] = ((pr1[52]*CL1[12]+pr1[53]*CL1[13])+(pr1[54]*CL1[14]+pr1[55]*CL1[15])) * pr2[*data2+4+48];
					dest[14] = ((pr1[56]*CL1[12]+pr1[57]*CL1[13])+(pr1[58]*CL1[14]+pr1[59]*CL1[15])) * pr2[*data2+8+48];
					dest[15] = ((pr1[60]*CL1[12]+pr1[61]*CL1[13])+(pr1[62]*CL1[14]+pr1[63]*CL1[15])) * pr2[*data2+12+48];

					dest+=16;
					data2++;
					}
				else if(*data2 == -4){//total ambiguity
					dest[0] = ( pr1[0]*CL1[0]+pr1[1]*CL1[1]+pr1[2]*CL1[2]+pr1[3]*CL1[3]);
					dest[1] = ( pr1[4]*CL1[0]+pr1[5]*CL1[1]+pr1[6]*CL1[2]+pr1[7]*CL1[3]);
					dest[2] = ( pr1[8]*CL1[0]+pr1[9]*CL1[1]+pr1[10]*CL1[2]+pr1[11]*CL1[3]);
					dest[3] = ( pr1[12]*CL1[0]+pr1[13]*CL1[1]+pr1[14]*CL1[2]+pr1[15]*CL1[3]);

					dest[4] = ( pr1[16]*CL1[4]+pr1[17]*CL1[5]+pr1[18]*CL1[6]+pr1[19]*CL1[7]);
					dest[5] = ( pr1[20]*CL1[4]+pr1[21]*CL1[5]+pr1[22]*CL1[6]+pr1[23]*CL1[7]);
					dest[6] = ( pr1[24]*CL1[4]+pr1[25]*CL1[5]+pr1[26]*CL1[6]+pr1[27]*CL1[7]);
					dest[7] = ( pr1[28]*CL1[4]+pr1[29]*CL1[5]+pr1[30]*CL1[6]+pr1[31]*CL1[7]);

					dest[8] = ( pr1[32]*CL1[8]+pr1[33]*CL1[9]+pr1[34]*CL1[10]+pr1[35]*CL1[11]);
					dest[9] = ( pr1[36]*CL1[8]+pr1[37]*CL1[9]+pr1[38]*CL1[10]+pr1[39]*CL1[11]);
					dest[10] = ( pr1[40]*CL1[8]+pr1[41]*CL1[9]+pr1[42]*CL1[10]+pr1[43]*CL1[11]);
					dest[11] = ( pr1[44]*CL1[8]+pr1[45]*CL1[9]+pr1[46]*CL1[10]+pr1[47]*CL1[11]);

					dest[12] = ( pr1[48]*CL1[12]+pr1[49]*CL1[13]+pr1[50]*CL1[14]+pr1[51]*CL1[15]);
					dest[13] = ( pr1[52]*CL1[12]+pr1[53]*CL1[13]+pr1[54]*CL1[14]+pr1[55]*CL1[15]);
					dest[14] = ( pr1[56]*CL1[12]+pr1[57]*CL1[13]+pr1[58]*CL1[14]+pr1[59]*CL1[15]);
					dest[15] = ( pr1[60]*CL1[12]+pr1[61]*CL1[13]+pr1[62]*CL1[14]+pr1[63]*CL1[15]);

					dest+=16;
					data2++;
					}
				else {//partial ambiguity
					//first figure in the ambiguous terminal
					int nstates=-1 * *(data2++);
					for(int j=0;j<16;j++) dest[j]=ZERO_POINT_ZERO;
					for(int s=0;s<nstates;s++){
						for(int r=0;r<4;r++){
							*(dest+(r*4)) += pr2[(*data2)+16*r];
							*(dest+(r*4)+1) += pr2[(*data2+4)+16*r];
							*(dest+(r*4)+2) += pr2[(*data2+8)+16*r];
							*(dest+(r*4)+3) += pr2[(*data2+12)+16*r];
							}
						data2++;
						}

					//now add the internal child
					*(dest++) *= ( pr1[0]*CL1[0]+pr1[1]*CL1[1]+pr1[2]*CL1[2]+pr1[3]*CL1[3]);
					*(dest++) *= ( pr1[4]*CL1[0]+pr1[5]*CL1[1]+pr1[6]*CL1[2]+pr1[7]*CL1[3]);
					*(dest++) *= ( pr1[8]*CL1[0]+pr1[9]*CL1[1]+pr1[10]*CL1[2]+pr1[11]*CL1[3]);
					*(dest++) *= ( pr1[12]*CL1[0]+pr1[13]*CL1[1]+pr1[14]*CL1[2]+pr1[15]*CL1[3]);

					*(dest++) *= ( pr1[16]*CL1[4]+pr1[17]*CL1[5]+pr1[18]*CL1[6]+pr1[19]*CL1[7]);
					*(dest++) *= ( pr1[20]*CL1[4]+pr1[21]*CL1[5]+pr1[22]*CL1[6]+pr1[23]*CL1[7]);
					*(dest++) *= ( pr1[24]*CL1[4]+pr1[25]*CL1[5]+pr1[26]*CL1[6]+pr1[27]*CL1[7]);
					*(dest++) *= ( pr1[28]*CL1[4]+pr1[29]*CL1[5]+pr1[30]*CL1[6]+pr1[31]*CL1[7]);

					*(dest++) *= ( pr1[32]*CL1[8]+pr1[33]*CL1[9]+pr1[34]*CL1[10]+pr1[35]*CL1[11]);
					*(dest++) *= ( pr1[36]*CL1[8]+pr1[37]*CL1[9]+pr1[38]*CL1[10]+pr1[39]*CL1[11]);
					*(dest++) *= ( pr1[40]*CL1[8]+pr1[41]*CL1[9]+pr1[42]*CL1[10]+pr1[43]*CL1[11]);
					*(dest++) *= ( pr1[44]*CL1[8]+pr1[45]*CL1[9]+pr1[46]*CL1[10]+pr1[47]*CL1[11]);

					*(dest++) *= ( pr1[48]*CL1[12]+pr1[49]*CL1[13]+pr1[50]*CL1[14]+pr1[51]*CL1[15]);
					*(dest++) *= ( pr1[52]*CL1[12]+pr1[53]*CL1[13]+pr1[54]*CL1[14]+pr1[55]*CL1[15]);
					*(dest++) *= ( pr1[56]*CL1[12]+pr1[57]*CL1[13]+pr1[58]*CL1[14]+pr1[59]*CL1[15]);
					*(dest++) *= ( pr1[60]*CL1[12]+pr1[61]*CL1[13]+pr1[62]*CL1[14]+pr1[63]*CL1[15]);
					}
				CL1+=16;
#ifdef ALLOW_SINGLE_SITE
				if(siteToScore > -1) break;
#endif
				}
			else{
				data2 = AdvanceDataPointer(data2, 1);
				}
			}
		}
	else{//general N rate version
#ifdef OMP_INTTERMCLA
		#pragma omp parallel for private(dest, CL1, data2)
		for(int i=0;i<nchar;i++){
			dest=&des[4*nRateCats*i];
			CL1=&CL[4*nRateCats*i];
			data2=&dat2[ambigMap[i]];
#else
		for(int i=0;i<nchar;i++){
#endif
#ifdef USE_COUNTS_IN_BOOT
			if(counts[i] > 0){
#else
			if(1){
#endif
				if(*data2 > -1){ //no ambiguity
					for(int r=0;r<nRateCats;r++){
						dest[0] = ( pr1[16*r+0]*CL1[4*r+0]+pr1[16*r+1]*CL1[4*r+1]+pr1[16*r+2]*CL1[4*r+2]+pr1[16*r+3]*CL1[4*r+3]) * pr2[(*data2)+16*r];
						dest[1] = ( pr1[16*r+4]*CL1[4*r+0]+pr1[16*r+5]*CL1[4*r+1]+pr1[16*r+6]*CL1[4*r+2]+pr1[16*r+7]*CL1[4*r+3]) * pr2[(*data2+4)+16*r];
						dest[2] = ( pr1[16*r+8]*CL1[4*r+0]+pr1[16*r+9]*CL1[4*r+1]+pr1[16*r+10]*CL1[4*r+2]+pr1[16*r+11]*CL1[4*r+3]) * pr2[(*data2+8)+16*r];
						dest[3] = ( pr1[16*r+12]*CL1[4*r+0]+pr1[16*r+13]*CL1[4*r+1]+pr1[16*r+14]*CL1[4*r+2]+pr1[16*r+15]*CL1[4*r+3]) * pr2[(*data2+12)+16*r];
						dest+=4;
						}
					data2++;
					}
				else if(*data2 == -4){//total ambiguity
					for(int r=0;r<nRateCats;r++){
						dest[0] = ( pr1[16*r+0]*CL1[4*r+0]+pr1[16*r+1]*CL1[4*r+1]+pr1[16*r+2]*CL1[4*r+2]+pr1[16*r+3]*CL1[4*r+3]);
						dest[1] = ( pr1[16*r+4]*CL1[4*r+0]+pr1[16*r+5]*CL1[4*r+1]+pr1[16*r+6]*CL1[4*r+2]+pr1[16*r+7]*CL1[4*r+3]);
						dest[2] = ( pr1[16*r+8]*CL1[4*r+0]+pr1[16*r+9]*CL1[4*r+1]+pr1[16*r+10]*CL1[4*r+2]+pr1[16*r+11]*CL1[4*r+3]);
						dest[3] = ( pr1[16*r+12]*CL1[4*r+0]+pr1[16*r+13]*CL1[4*r+1]+pr1[16*r+14]*CL1[4*r+2]+pr1[16*r+15]*CL1[4*r+3]);
						dest+=4;
						}
					data2++;
					}
				else {//partial ambiguity
					//first figure in the ambiguous terminal
					int nstates=-1 * *(data2++);
					for(int q=0;q<4*nRateCats;q++) dest[q]=0;
					for(int s=0;s<nstates;s++){
						for(int r=0;r<nRateCats;r++){
							*(dest+(r*4)) += pr2[(*data2)+16*r];
							*(dest+(r*4)+1) += pr2[(*data2+4)+16*r];
							*(dest+(r*4)+2) += pr2[(*data2+8)+16*r];
							*(dest+(r*4)+3) += pr2[(*data2+12)+16*r];
							}
						data2++;
						}
					//now add the internal child
					for(int r=0;r<nRateCats;r++){
						*(dest++) *= ( pr1[16*r+0]*CL1[4*r+0]+pr1[16*r+1]*CL1[4*r+1]+pr1[16*r+2]*CL1[4*r+2]+pr1[16*r+3]*CL1[4*r+3]);
						*(dest++) *= ( pr1[16*r+4]*CL1[4*r+0]+pr1[16*r+5]*CL1[4*r+1]+pr1[16*r+6]*CL1[4*r+2]+pr1[16*r+7]*CL1[4*r+3]);
						*(dest++) *= ( pr1[16*r+8]*CL1[4*r+0]+pr1[16*r+9]*CL1[4*r+1]+pr1[16*r+10]*CL1[4*r+2]+pr1[16*r+11]*CL1[4*r+3]);
						*(dest++) *= ( pr1[16*r+12]*CL1[4*r+0]+pr1[16*r+13]*CL1[4*r+1]+pr1[16*r+14]*CL1[4*r+2]+pr1[16*r+15]*CL1[4*r+3]);
						}
					}
				CL1 += 4*nRateCats;
#ifdef ALLOW_SINGLE_SITE
				if(siteToScore > -1) break;
#endif
				}
			else{
				data2 = AdvanceDataPointer(data2, 1);
				}
			}
		}

	for(int i=0;i<nchar;i++)
		destCLA->underflow_mult[i]=LCLA->underflow_mult[i];

	destCLA->rescaleRank=LCLA->rescaleRank+2;
	}

void Tree::CalcFullCLAInternalTerminalNState(CondLikeArray *destCLA, const CondLikeArray *LCLA, const FLOAT_TYPE *pr1, const FLOAT_TYPE *pr2, char *dat2){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *des=destCLA->arr;
	FLOAT_TYPE *dest=des;
	const FLOAT_TYPE *CL=LCLA->arr;
	const FLOAT_TYPE *CL1=CL;
	const char *data2=dat2;

	const int nchar = data->NChar();
	const int nRateCats = mod->NRateCats();
	const int nstates = mod->NStates();
	const int *counts = data->GetCounts();

#ifdef UNIX
	madvise(dest, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void*)CL1, nchar*nstates*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

	if(siteToScore > 0) data2 += siteToScore;

#ifdef OMP_INTTERMCLA_NSTATE
	#pragma omp parallel for private(dest, CL1, data2)
	for(int i=0;i<nchar;i++){
		dest=&des[nRateCats*nstates*i];
		CL1=&CL[nRateCats*nstates*i];
		data2=&dat2[i];
#else
	for(int i=0;i<nchar;i++){
#endif
#ifdef USE_COUNTS_IN_BOOT
		if(counts[i]> 0){
#else
		if(1){
#endif
			for(int rate=0;rate<nRateCats;rate++){
				for(int from=0;from<nstates;from++){
					FLOAT_TYPE d = ZERO_POINT_ZERO;
					for(int to=0;to<nstates;to++){
						d += pr1[rate*nstates*nstates + from*nstates + to] * CL1[to];
						}
					dest[from] = (*data2 < nstates ? d * pr2[rate*nstates*nstates + (*data2)+from*nstates] : d);
					}
				assert(dest[19] < 1e10);
				dest += nstates;
				CL1 += nstates;
				}
			data2++;
#ifdef ALLOW_SINGLE_SITE
			if(siteToScore > -1) break;
#endif
			}
		else data2++;
		}

	for(int i=0;i<nchar;i++)
		destCLA->underflow_mult[i]=LCLA->underflow_mult[i];

	destCLA->rescaleRank=LCLA->rescaleRank+2;
	}

void Tree::CalcFullCLAPartialInternalRateHet(CondLikeArray *destCLA, const CondLikeArray *LCLA, const FLOAT_TYPE *pr1, CondLikeArray *partialCLA){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *dest=destCLA->arr;
	FLOAT_TYPE *CL1=LCLA->arr;
	FLOAT_TYPE *partial=partialCLA->arr;

	const int nchar = data->NChar();
	const int nRateCats = mod->NRateCats();

#ifdef UNIX
	madvise(dest, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void*)CL1, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise(partial, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

	if(nRateCats==4){
		for(int i=0;i<nchar;i++){
			*(dest++) = ( pr1[0]*CL1[0]+pr1[1]*CL1[1]+pr1[2]*CL1[2]+pr1[3]*CL1[3]) * *(partial++);
			*(dest++) = ( pr1[4]*CL1[0]+pr1[5]*CL1[1]+pr1[6]*CL1[2]+pr1[7]*CL1[3]) * *(partial++);
			*(dest++) = ( pr1[8]*CL1[0]+pr1[9]*CL1[1]+pr1[10]*CL1[2]+pr1[11]*CL1[3]) * *(partial++);
			*(dest++) = ( pr1[12]*CL1[0]+pr1[13]*CL1[1]+pr1[14]*CL1[2]+pr1[15]*CL1[3]) * *(partial++);

			*(dest++) = ( pr1[16]*CL1[4]+pr1[17]*CL1[5]+pr1[18]*CL1[6]+pr1[19]*CL1[7]) * *(partial++);
			*(dest++) = ( pr1[20]*CL1[4]+pr1[21]*CL1[5]+pr1[22]*CL1[6]+pr1[23]*CL1[7]) * *(partial++);
			*(dest++) = ( pr1[24]*CL1[4]+pr1[25]*CL1[5]+pr1[26]*CL1[6]+pr1[27]*CL1[7]) * *(partial++);
			*(dest++) = ( pr1[28]*CL1[4]+pr1[29]*CL1[5]+pr1[30]*CL1[6]+pr1[31]*CL1[7]) * *(partial++);

			*(dest++) = ( pr1[32]*CL1[8]+pr1[33]*CL1[9]+pr1[34]*CL1[10]+pr1[35]*CL1[11]) * *(partial++);
			*(dest++) = ( pr1[36]*CL1[8]+pr1[37]*CL1[9]+pr1[38]*CL1[10]+pr1[39]*CL1[11]) * *(partial++);
			*(dest++) = ( pr1[40]*CL1[8]+pr1[41]*CL1[9]+pr1[42]*CL1[10]+pr1[43]*CL1[11]) * *(partial++);
			*(dest++) = ( pr1[44]*CL1[8]+pr1[45]*CL1[9]+pr1[46]*CL1[10]+pr1[47]*CL1[11]) * *(partial++);

			*(dest++) = ( pr1[48]*CL1[12]+pr1[49]*CL1[13]+pr1[50]*CL1[14]+pr1[51]*CL1[15]) * *(partial++);
			*(dest++) = ( pr1[52]*CL1[12]+pr1[53]*CL1[13]+pr1[54]*CL1[14]+pr1[55]*CL1[15]) * *(partial++);
			*(dest++) = ( pr1[56]*CL1[12]+pr1[57]*CL1[13]+pr1[58]*CL1[14]+pr1[59]*CL1[15]) * *(partial++);
			*(dest++) = ( pr1[60]*CL1[12]+pr1[61]*CL1[13]+pr1[62]*CL1[14]+pr1[63]*CL1[15]) * *(partial++);
			CL1+=16;
			assert(*(dest-1)>ZERO_POINT_ZERO);
			}
		}
	else{
		for(int i=0;i<nchar;i++){
			for(int r=0;r<nRateCats;r++){
				*(dest++) = ( pr1[16*r+0]*CL1[4*r+0]+pr1[16*r+1]*CL1[4*r+1]+pr1[16*r+2]*CL1[4*r+2]+pr1[16*r+3]*CL1[4*r+3]) * *(partial++);
				*(dest++) = ( pr1[16*r+4]*CL1[4*r+0]+pr1[16*r+5]*CL1[4*r+1]+pr1[16*r+6]*CL1[4*r+2]+pr1[16*r+7]*CL1[4*r+3]) * *(partial++);
				*(dest++) = ( pr1[16*r+8]*CL1[4*r+0]+pr1[16*r+9]*CL1[4*r+1]+pr1[16*r+10]*CL1[4*r+2]+pr1[16*r+11]*CL1[4*r+3]) * *(partial++);
				*(dest++) = ( pr1[16*r+12]*CL1[4*r+0]+pr1[16*r+13]*CL1[4*r+1]+pr1[16*r+14]*CL1[4*r+2]+pr1[16*r+15]*CL1[4*r+3]) * *(partial++);
				CL1+=4;
				assert(*(dest-1)>ZERO_POINT_ZERO);
				}
			}
		}

	for(int site=0;site<nchar;site++){
		destCLA->underflow_mult[site]=partialCLA->underflow_mult[site] + LCLA->underflow_mult[site];
		}
	}

void Tree::CalcFullCLAPartialTerminalRateHet(CondLikeArray *destCLA, const CondLikeArray *partialCLA, const FLOAT_TYPE *Lpr, char *Ldata){
	//this function assumes that the pmat is arranged with the 16 entries for the
	//first rate, followed by 16 for the second, etc.
	FLOAT_TYPE *dest=destCLA->arr;
	FLOAT_TYPE *partial=partialCLA->arr;

	const int nchar = data->NChar();
	const int nRateCats = mod->NRateCats();

#ifdef UNIX
	madvise(dest, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
	madvise((void*)partial, nchar*4*nRateCats*sizeof(FLOAT_TYPE), MADV_SEQUENTIAL);
#endif

	for(int i=0;i<nchar;i++){
		if(*Ldata > -1){ //no ambiguity
			for(int i=0;i<nRateCats;i++){
				*(dest++) = Lpr[(*Ldata)+16*i] * *(partial++);
				*(dest++) = Lpr[(*Ldata+4)+16*i] * *(partial++);
				*(dest++) = Lpr[(*Ldata+8)+16*i] * *(partial++);
				*(dest++) = Lpr[(*Ldata+12)+16*i] * *(partial++);
//				assert(*(dest-1)>ZERO_POINT_ZERO);
				}
			Ldata++;
			}

		else if(*Ldata == -4){ //total ambiguity
			for(int i=0;i<4*nRateCats;i++) *(dest++) = *(partial++);
			Ldata++;
			}
		else{ //partial ambiguity
			//first figure in the ambiguous terminal
			char nstates=-1 * *(Ldata++);
			for(int q=0;q<4*nRateCats;q++) dest[q]=0;
			for(int i=0;i<nstates;i++){
				for(int i=0;i<nRateCats;i++){
					*(dest+(i*4)) += Lpr[(*Ldata)+16*i];
					*(dest+(i*4)+1) += Lpr[(*Ldata+4)+16*i];
					*(dest+(i*4)+2) += Lpr[(*Ldata+8)+16*i];
					*(dest+(i*4)+3) += Lpr[(*Ldata+12)+16*i];
//					assert(*(dest-1)>ZERO_POINT_ZERO);
					}
				Ldata++;
				}

			//now add the partial
			for(int r=0;r<nRateCats;r++){
				*(dest++) *= *(partial++);
				*(dest++) *= *(partial++);
				*(dest++) *= *(partial++);
				*(dest++) *= *(partial++);
				}
			}
		}
	for(int i=0;i<nchar;i++)
		destCLA->underflow_mult[i]=partialCLA->underflow_mult[i];
}

//SINGLE SITE FUNCTIONS

pair<FLOAT_TYPE, FLOAT_TYPE> Tree::OptimizeSingleSiteTreeScale(FLOAT_TYPE optPrecision){
	//this is silly, but the site likelihood calculating function will do it for the
	//correct single site, but using the pattern count of the first character.  So, we'll
	//need to divide by this count to get the proper site like
	FLOAT_TYPE siteCount = (FLOAT_TYPE) data->Count(0);
	Score();
	FLOAT_TYPE prev=lnL/siteCount;
	FLOAT_TYPE cur;
	FLOAT_TYPE scale;
	FLOAT_TYPE t;
	FLOAT_TYPE lastChange=(FLOAT_TYPE)9999.9;
	FLOAT_TYPE effectiveScale = ONE_POINT_ZERO; //this measures the change in scale relative to what it began at.
	FLOAT_TYPE upperBracket = FLT_MAX;   //the smallest value we know of with a negative d1 (relative to inital scale of 1.0!)
	FLOAT_TYPE lowerBracket = FLT_MIN;   //the largest value we know of with a positive d1 (relative to inital scale of 1.0!)
	FLOAT_TYPE incr;

#ifdef DEBUG_SCALE_OPT
	ofstream deb("scaleTrace.log");
	deb.precision(20);
	for(int s=0;s<50;s++){
		FLOAT_TYPE scale=0.5 + s*.025;
		ScaleWholeTree(scale);
		Score();
		deb << scale << "\t" << lnL << endl;
		ScaleWholeTree(ONE_POINT_ZERO/scale);
		}
	deb.close();
#endif

	if(FloatingPointEquals(lnL, ZERO_POINT_ZERO, max(1.0e-8, GARLI_FP_EPS * 2.0))){
		return make_pair<FLOAT_TYPE, FLOAT_TYPE>(-ONE_POINT_ZERO, ZERO_POINT_ZERO);
		}

	int pass=1;
	while(1){
		//reversed this now so the reduction in scale is done first when getting the
		//derivs.  This works better if some blens are at DEF_MAX_BLEN because the
		//scaling up causes them to hit the max and the relative blens to change

#ifdef SINGLE_PRECISION_FLOATS
		incr=0.005f;
#else
		incr=0.005;
#endif

		scale=ONE_POINT_ZERO-incr;

		ScaleWholeTree(scale);
		Score();
		cur=lnL/siteCount;
		ScaleWholeTree(ONE_POINT_ZERO/scale);//return the tree to its original scale
		FLOAT_TYPE d12=(cur-prev)/-incr;

		if(pass == 1 && fabs(d12) < max(1.0e-8, GARLI_FP_EPS * 2.0)){
			//The surface looks suspiciously flat.  Test if the likelihood
			//is really invariant for different scales (which means that
			//the site is all missing or only has an observed state for one taxon)
			ScaleWholeTree(1.1);
			Score();
			FLOAT_TYPE s = lnL/siteCount;
			ScaleWholeTree(1.0/1.1);
			if(fabs(prev - s) < max(1.0e-8, GARLI_FP_EPS * 2.0)) return make_pair<FLOAT_TYPE, FLOAT_TYPE>(-ONE_POINT_ZERO, prev);
			}

		scale=ONE_POINT_ZERO + incr;
		ScaleWholeTree(scale);
		Score();
		cur=lnL/siteCount;
		ScaleWholeTree(ONE_POINT_ZERO/scale);//return the tree to its original scale
		FLOAT_TYPE d11=(cur-prev)/incr;

		FLOAT_TYPE d1=(d11+d12)*ZERO_POINT_FIVE;
		FLOAT_TYPE d2=(d11-d12)/incr;

		FLOAT_TYPE est = -d1/d2;
		FLOAT_TYPE estImprove = d1*est + d2*(est*est*ZERO_POINT_FIVE);

		//return conditions
		if(estImprove < optPrecision && d2 < ZERO_POINT_ZERO){
			ScaleWholeTree(ONE_POINT_ZERO/effectiveScale);
			return make_pair<FLOAT_TYPE, FLOAT_TYPE>(effectiveScale, prev);
			}

		if(d2 < ZERO_POINT_ZERO){
			est = max(min((FLOAT_TYPE)0.5, est), (FLOAT_TYPE)-0.5);
			t=ONE_POINT_ZERO + est;
			}
		else{
			if(d1 > ZERO_POINT_ZERO) t=(FLOAT_TYPE)2.0;
			else t=(FLOAT_TYPE)0.5;
			}

		//update the brackets
		if(d1 <= ZERO_POINT_ZERO && effectiveScale < upperBracket)
			upperBracket = effectiveScale;
		else if(d1 > ZERO_POINT_ZERO && effectiveScale > lowerBracket)
			lowerBracket = effectiveScale;

		//if the surface is wacky and we are going to shoot past one of our brackets
		//take evasive action by going halfway to the bracket
		if((effectiveScale * t) <= lowerBracket){
			t = (lowerBracket + effectiveScale) * ZERO_POINT_FIVE / effectiveScale;
			}
		else if((effectiveScale * t) >= upperBracket){
			t = (upperBracket + effectiveScale) * ZERO_POINT_FIVE / effectiveScale;
			}

		scale=t;
		effectiveScale *= scale;
		if(effectiveScale > 100.0) return make_pair<FLOAT_TYPE, FLOAT_TYPE>(100.0, prev);
		ScaleWholeTree(scale);
		if(effectiveScale < 1e-4){
			//The rate is essentially zero.  Invariant sites should be getting caught
			//before even calling this func, so this probably won't be visited
			ScaleWholeTree(1.0/effectiveScale);
			return make_pair<FLOAT_TYPE, FLOAT_TYPE>(effectiveScale, prev);
			}

		Score();
		cur=lnL/siteCount;
		lastChange = cur - prev;
		prev=cur;
		pass++;
		}
	assert(0);
	}

