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

#include "defs.h"
#include "bipartition.h"
#include "reconnode.h"

int Bipartition::nBlocks;
int Bipartition:: blockBits;
int Bipartition::ntax;
unsigned int Bipartition::largestBlockDigit;
unsigned int Bipartition::allBitsOn;
char * Bipartition::str;
unsigned int Bipartition::partialBlockMask;

Bipartition::~Bipartition(){
	if(rep!=NULL) delete []rep;
	rep=NULL;
	}	

//note that this is "less than" for sorting purposes, not in a subset sense
bool BipartitionLessThan(const Bipartition &lhs, const Bipartition &rhs){
	int i;
	for(i=0;i<Bipartition::nBlocks-1;i++){
		if(lhs.rep[i] > rhs.rep[i]) return false;
		else if(lhs.rep[i] < rhs.rep[i]) return true;
		}
		
	if(((lhs.rep[i]) & lhs.partialBlockMask) > ((rhs.rep[i]) & lhs.partialBlockMask)) return false;
	else return true;
	}

void Bipartition::SetBipartitionStatics(int nt){
	Bipartition::blockBits=sizeof(int)*8;
	Bipartition::ntax=nt;
	Bipartition::nBlocks=(int)ceil((FLOAT_TYPE)nt/(FLOAT_TYPE)Bipartition::blockBits);
	Bipartition::largestBlockDigit=1<<(Bipartition::blockBits-1);
	Bipartition::allBitsOn=(unsigned int)(pow(2.0, Bipartition::blockBits)-1);
	Bipartition::str=new char[nt+1];
	Bipartition::str[nt] = '\0';
	Bipartition::SetPartialBlockMask();	
	}

void Bipartition::SetPartialBlockMask(){
		partialBlockMask=0;
		unsigned int bit=largestBlockDigit;
		for(int b=0;b<ntax%blockBits;b++){
			partialBlockMask += bit;
			bit = bit >> 1;
			}
		if(ntax%blockBits == 0) partialBlockMask=allBitsOn;
		}

//this function does 2 things
//1. Fills this bipartition with the bitwise intersection of a backbone mask and a mask
//representing a subset of taxa in a growing tree.  Note that it is safe to call this when the
//constraint is not a backbone and/or when the partialMask is NULL.  In that case it will fill
//the bipartition with one or the other, or with all bits on if their if neither 
//2. Checks if there is a meaningful intersection between the created joint mask and 
//the constraint.  That means at least 2 bits are "on" on each site of the constrained bipartition
bool Bipartition::MakeJointMask(const Constraint &constr, const Bipartition *partialMask){
	if(constr.IsBackbone()){
		//this just uses Bipartition::Operator=()
		*this = *(constr.GetBackboneMask());
		if(partialMask != NULL)
			this->AndEquals(*partialMask);
		}
	else if(partialMask != NULL){
		*this = *(partialMask);
		}
	else FillAllBits();

	Bipartition temp;
	temp = constr.GetBipartition();
	temp.AndEquals(*this);
	if(temp.CountOnBits() < 2) return false;
	temp = constr.GetBipartition();
	temp.Complement();
	temp.AndEquals(*this);
	if(temp.CountOnBits() < 2) return false;
	return true;
	}