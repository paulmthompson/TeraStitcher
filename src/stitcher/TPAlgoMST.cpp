//------------------------------------------------------------------------------------------------
// Copyright (c) 2012  Alessandro Bria and Giulio Iannello (University Campus Bio-Medico of Rome).  
// All rights reserved.
//------------------------------------------------------------------------------------------------

/*******************************************************************************************************************************************************************************************
*    LICENSE NOTICE
********************************************************************************************************************************************************************************************
*    By downloading/using/running/editing/changing any portion of codes in this package you agree to this license. If you do not agree to this license, do not download/use/run/edit/change
*    this code.
********************************************************************************************************************************************************************************************
*    1. This material is free for non-profit research, but needs a special license for any commercial purpose. Please contact Alessandro Bria at a.bria@unicas.it or Giulio Iannello at 
*       g.iannello@unicampus.it for further details.
*    2. You agree to appropriately cite this work in your related studies and publications.
*
*       Bria, A., Iannello, G., "TeraStitcher - A Tool for Fast 3D Automatic Stitching of Teravoxel-sized Microscopy Images", (2012) BMC Bioinformatics, 13 (1), art. no. 316.
*
*    3. This material is provided by  the copyright holders (Alessandro Bria  and  Giulio Iannello),  University Campus Bio-Medico and contributors "as is" and any express or implied war-
*       ranties, including, but  not limited to,  any implied warranties  of merchantability,  non-infringement, or fitness for a particular purpose are  disclaimed. In no event shall the
*       copyright owners, University Campus Bio-Medico, or contributors be liable for any direct, indirect, incidental, special, exemplary, or  consequential  damages  (including, but not 
*       limited to, procurement of substitute goods or services; loss of use, data, or profits;reasonable royalties; or business interruption) however caused  and on any theory of liabil-
*       ity, whether in contract, strict liability, or tort  (including negligence or otherwise) arising in any way out of the use of this software,  even if advised of the possibility of
*       such damage.
*    4. Neither the name of University  Campus Bio-Medico of Rome, nor Alessandro Bria and Giulio Iannello, may be used to endorse or  promote products  derived from this software without
*       specific prior written permission.
********************************************************************************************************************************************************************************************/

#include <limits>
#include "TPAlgoMST.h"
#include "S_config.h"
#include "volumemanager.config.h"
#include "vmStackedVolume.h"
#include "vmVirtualStack.h"
#include "Displacement.h"

using namespace volumemanager;
using namespace iomanager;

//triplet data type definition
typedef struct 							
{
	float V;
	float H;
	float D;
}triplet;

TPAlgoMST::TPAlgoMST(VirtualVolume * _volume) : TPAlgo(S_FATPM_SP_TREE, _volume)
{
	#if S_VERBOSE>4
	printf("........in TPAlgoMST::TPAlgoMST(VirtualVolume * _volume)");
	#endif
}

/*************************************************************************************************************
* Finds the optimal tile placement on the <volume> object member via Minimum Spanning Tree.
* Stacks matrix is considered as a graph  where  displacements are edges a nd stacks are nodes. The inverse of
* displacements reliability factors are  edge weights,  so that a totally unreliable displacement has a weight
* 1/0.0 = +inf and a totally reliable displacement has a weight 1/1.0 = 1. Thus, weights will be in [1, +inf].
* After computing the MST, the absolute tile positions are obtained from a stitchable source stack by means of
* navigating the MST and using the selected displacements.
* PROs: very general method; it ignores bad displacements since they have +inf weight.
* CONs: the best path between two adjacent stacks can pass through many stacks even if the rejected displacem-
*       ent is quite reliable, with a very little reliability gain.  This implies possible bad absolute  posi-
*       tions estimations when the path is too long.
**************************************************************************************************************/
void TPAlgoMST::execute()
{
	#if S_VERBOSE > 2
	printf("....in TPAlgoMST::execute()");
	#endif

	float ***D;									//distances matrix for VHD directions
	std::pair<int*,int*> **predecessors;	//predecessor matrix for VHD directions
	int src_row=0, src_col=0;						//source vertex

	//0) fixing the source as the stitchable VirtualStack nearest to the top-left corner
	float min_distance = std::numeric_limits<float>::infinity();
	for(int row=0; row<volume->getN_ROWS(); row++)
		for(int col=0; col<volume->getN_COLS(); col++)
			if(volume->getSTACKS()[row][col]->isStitchable() && sqrt((float)(row*row+col*col)) < min_distance )
			{
				src_row = row;
				src_col = col;
				min_distance = sqrt((float)(row*row+col*col));
			}
	#if S_VERBOSE > 4
	printf("....in TPAlgoMST::execute(): SOURCE is [%d,%d]\n",src_row,src_col);
	#endif

	//1) initializing distance and predecessor matrix
	D = new float **[volume->getN_ROWS()];
	predecessors = new std::pair<int*,int*> *[volume->getN_ROWS()];
	for(int row=0; row<volume->getN_ROWS(); row++)
	{
		D[row] = new float*[volume->getN_COLS()];
		predecessors[row] = new std::pair<int*,int*>[volume->getN_COLS()];
		for(int col=0; col<volume->getN_COLS(); col++)
		{
			D[row][col] = new float[3];
			predecessors[row][col].first = new int[3];
			predecessors[row][col].second = new int[3];
			for(int k=0; k<3; k++)
			{
				D[row][col][k] = std::numeric_limits<float>::infinity();
				predecessors[row][col].first[k] = predecessors[row][col].second[k] = -1;
			}
		}
	}
	D[src_row][src_col][0] = D[src_row][src_col][1] = D[src_row][src_col][2] = 0.0F;

	//2) processing edges in order to obtain distance matrix
	for(int vertices=1; vertices <= volume->getN_ROWS()*volume->getN_COLS(); vertices++)
		for(int E_row = 0; E_row<volume->getN_ROWS(); E_row++)
			for(int E_col = 0; E_col<volume->getN_COLS(); E_col++)
				for(int k=0; k<3; k++)
				{
					if(E_row != volume->getN_ROWS() -1 )	//NORTH-SOUTH displacements
					{
						float weight = SAFE_DIVIDE(1, volume->getSTACKS()[E_row  ][E_col]->getSOUTH()[0]->getReliability(direction(k)), S_UNRELIABLE_WEIGHT);
						if(D[E_row][E_col][k] + weight < D[E_row+1][E_col][k])
						{
							D[E_row+1][E_col][k] = D[E_row][E_col][k] + weight;
							predecessors[E_row+1][E_col].first[k] = E_row;
							predecessors[E_row+1][E_col].second[k] = E_col;
						}
						if(D[E_row+1][E_col][k] + weight < D[E_row][E_col][k])
						{
							D[E_row][E_col][k] = D[E_row+1][E_col][k] + weight;
							predecessors[E_row][E_col].first[k] = E_row+1;
							predecessors[E_row][E_col].second[k]= E_col;
						}
					}
					if(E_col != volume->getN_COLS() -1 )	//EAST-WEST displacements
					{
						float weight = SAFE_DIVIDE(1, volume->getSTACKS()[E_row  ][E_col]->getEAST()[0]->getReliability(direction(k)), S_UNRELIABLE_WEIGHT);
						if(D[E_row][E_col][k] + weight < D[E_row][E_col+1][k])
						{
							D[E_row][E_col+1][k] = D[E_row][E_col][k] + weight;
							predecessors[E_row][E_col+1].first[k] = E_row;
							predecessors[E_row][E_col+1].second[k]= E_col;
						}
						if(D[E_row][E_col+1][k] + weight < D[E_row][E_col][k])
						{
							D[E_row][E_col][k] = D[E_row][E_col+1][k] + weight;
							predecessors[E_row][E_col].first[k] = E_row;
							predecessors[E_row][E_col].second[k] = E_col+1;
						}
					}
				}

	#if S_VERBOSE > 4
	for(int k=0; k<3; k++)
	{
		printf("\n\n....in TPAlgoMST::execute(): %d DIRECTION:\n",  k);
		printf("\n\t");
		for(int col=0; col < volume->getN_COLS(); col++)
			printf("[%d]\t\t", col);
		printf("\n\n\n");
		for(int row = 0; row < volume->getN_ROWS(); row++)
		{
			printf("[%d]\t",row);
			for(int col= 0; col < volume->getN_COLS(); col++)
				printf("(%d,%d)\t", predecessors[row][col].first[k], predecessors[row][col].second[k]);
			printf("\n\n\n");
		}
		printf("\n");
		printf("\n\t");
		for(int col=0; col < volume->getN_COLS(); col++)
			printf("[%d]\t\t", col);
		printf("\n\n\n");
		for(int row = 0; row < volume->getN_ROWS(); row++)
		{
			printf("[%d]\t",row);
			for(int col= 0; col < volume->getN_COLS(); col++)
				printf("%8.3f\t", D[row][col][k]);
			printf("\n\n\n");
		}
		printf("\n");
	}
	#endif

	//3) resetting to 0 all stacks absolute coordinates
	for(int row = 0; row<volume->getN_ROWS(); row++)
		for(int col = 0; col<volume->getN_COLS(); col++)
		{
			volume->getSTACKS()[row][col]->setABS_V(0);
			volume->getSTACKS()[row][col]->setABS_H(0);
			volume->getSTACKS()[row][col]->setABS_D(0);
		}

	//4) assigning absolute coordinates using predecessors matrix
	for(int row = 0; row<volume->getN_ROWS(); row++)
	{
		for(int col = 0; col<volume->getN_COLS(); col++)
		{
			if(!(row == src_row && col == src_col))
			{
				for(int k=0; k<3; k++)
				{
					VirtualStack *source, *dest, *v, *u;
					source = volume->getSTACKS()[src_row][src_col];
					dest   = volume->getSTACKS()[row][col];
					v      = dest;

					#if S_VERBOSE > 4
					printf("S[%d,%d] [%d]_path:\n", k, row, col);
					#endif
					while (v != source)
					{
						int u_row, u_col;
						u_row = predecessors[v->getROW_INDEX()][v->getCOL_INDEX()].first[k];
						if(u_row>= volume->getN_ROWS() || u_row < 0)
							throw iom::exception("...in TPAlgoMST::execute(): error in the predecessor matrix");
						u_col = (int) predecessors[v->getROW_INDEX()][v->getCOL_INDEX()].second[k];
						if(u_col>= volume->getN_COLS() || u_col < 0)
							throw iom::exception("...in TPAlgoMST::execute(): error in the predecessor matrix");
						u = volume->getSTACKS()[u_row][u_col ];

						#if S_VERBOSE > 4
						printf("\t[%d,%d] (ABS_[%d] = %d %+d)\n",u->getROW_INDEX(), u->getCOL_INDEX(), k, dest->getABS(k), u->getDisplacement(v)->getDisplacement(direction(k)));
						#endif
						dest->setABS(dest->getABS(k) + u->getDisplacement(v)->getDisplacement(direction(k)), k);
						v = u;

						if(dest->isStitchable() && !(v->isStitchable()))
							printf("\nWARNING! in TPAlgoMST::execute(): direction %d: VirtualStack [%d,%d] is passing through VirtualStack [%d,%d], that is NOT STITCHABLE\n", k, row, col, v->getROW_INDEX(), v->getCOL_INDEX());
					}
					#if S_VERBOSE > 4
					printf("\n");
					#endif
				}
				#if S_VERBOSE > 4
				system("PAUSE");
				#endif
			}
		}
	}

	//5) translating stacks absolute coordinates by choosing VirtualStack[0][0] as the new source
	int trasl_X = volume->getSTACKS()[0][0]->getABS_V();
	int trasl_Y = volume->getSTACKS()[0][0]->getABS_H();
	int trasl_Z = volume->getSTACKS()[0][0]->getABS_D();
	for(int row = 0; row<volume->getN_ROWS(); row++)
	{
		for(int V_col = 0; V_col<volume->getN_COLS(); V_col++)
		{
			volume->getSTACKS()[row][V_col]->setABS_V(volume->getSTACKS()[row][V_col]->getABS_V()-trasl_X);
			volume->getSTACKS()[row][V_col]->setABS_H(volume->getSTACKS()[row][V_col]->getABS_H()-trasl_Y);
			volume->getSTACKS()[row][V_col]->setABS_D(volume->getSTACKS()[row][V_col]->getABS_D()-trasl_Z);
		}
	}


	//deallocating distance matrix and predecessor matrix
	for(int row=0; row<volume->getN_ROWS(); row++)
	{
		for(int col=0; col<volume->getN_COLS(); col++)
		{
			delete[] D[row][col];
			delete[] predecessors[row][col].first;
			delete[] predecessors[row][col].second;
		}
		delete[] D[row];
		delete[] predecessors[row];
	}
	delete[] D;
	delete[] predecessors;
}
