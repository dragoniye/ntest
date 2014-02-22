// Copyright Chris Welty
//	All Rights Reserved
// This file is distributed subject to GNU GPL version 3. See the files
// GPLv3.txt and License.txt in the instructions subdirectory for details.

// Evaluator source code
#include <sstream>
#include "core/QPosition.h"
#include "n64/bitextractor.h"
#include "pattern/types.h"

#include "Evaluator.h"

using namespace std;

//////////////////////////////////////////////////
// Evaluator base class
//////////////////////////////////////////////////

class CEvaluatorList: public std::map<CEvaluatorInfo, CEvaluator*> {
public:
	~CEvaluatorList();
};

static CEvaluatorList evaluatorList;

CEvaluatorList::~CEvaluatorList() {
	for (iterator it=begin(); it!=end(); it++) {
		delete it->second;
	}
}

CEvaluator* CEvaluator::FindEvaluator(char evaluatorType, char coeffSet) {
	CEvaluatorInfo evaluatorInfo;
	CEvaluator* result;
	map<CEvaluatorInfo, CEvaluator*>::iterator ptr;

	evaluatorInfo.evaluatorType=evaluatorType;
	evaluatorInfo.coeffSet=coeffSet;

	ptr=evaluatorList.find(evaluatorInfo);
	if (ptr==evaluatorList.end()) {
		switch(evaluatorType) {
		case 'J': {
			int nFiles= (coeffSet>='9')?10:6;
			result=new CEvaluator(FNBase(evaluatorType, coeffSet), nFiles);
			break;
				  }
		default:
			assert(0);
		}
		assert(result);
		evaluatorList[evaluatorInfo]=result;
	}
	else {
		result=(*ptr).second;
	}
	return result;
}

std::string CEvaluator::FNBase(char evaluatorType, char coeffSet) {
	std::ostringstream os;
	os << "coefficients/" << evaluatorType << coeffSet;
	return os.str();
}

//////////////////////////////////////////////////////
// Pattern J evaluator
//	Use 2x4, 2x5, edge+X patterns
//////////////////////////////////////////////////////

//! Conver the file to i2 format
//!
//! read in the file (float format) and write it out in i2 format.
//! reopen the file and read the iversion and fParams flags
static void ConvertFile(FILE*& fp, std::string fn, int& iVersion, u4& fParams) {
	// convert float coefficient file to int. Write new coefficients file to disk and reload.
	std::vector<i2> newCoeffs;
	float oldCoeff;
	while (fread(&oldCoeff, sizeof(oldCoeff), 1, fp)) {
		int coeff=int(oldCoeff*kStoneValue);
		if (coeff>0x3FFF)
			coeff=0x3FFF;
		if (coeff<-0x3FFF)
			coeff=-0x3FFF;
		newCoeffs.push_back(i2(coeff));
	}
	fclose(fp);
	fp=fopen(fn.c_str(), "wb");
	if (!fp)
		throw std::string("Can't open coefficient file for conversion: ")+fn;
	fParams=100;
	fwrite(&iVersion, sizeof(int), 1, fp);
	fwrite(&fParams, sizeof(int), 1, fp);
	fwrite(&newCoeffs[0], sizeof(u2), newCoeffs.size(), fp);
	fclose(fp);
	fp=fopen(fn.c_str(), "rb");
	if (!fp)
		throw std::string("Can't open coefficient file ")+fn;
	size_t result=fread(&iVersion, sizeof(int), 1, fp);
	assert(result==1);
	result=fread(&fParams, sizeof(int), 1, fp);
	assert(result==1);
}

//! Read in Evaluator coefficients from a coefficient file
//!
//! If the file's fParams is 14, coefficients are stored as floats and are in units of stones
//! If the file's fParams is 100, coefficients are stored as u2s and are in units of centistones.
//!
//! \throw string if error
CEvaluator::CEvaluator(const std::string& fnBase, int nFiles) {
	int map,  iFile, coeffStart, packedCoeff;
	int nIDs, nConfigs, id, config, cid;
	u2 configpm1, configpm2, mapsize;
	bool fHasPotMobs;
	float* rawCoeffs=0;	//!< for use with raw (float) coeffs
	i2* i2Coeffs=0;		//!< for use with converted (packed) coeffs
	TCoeff coeff;
	int iSubset, nSubsets, nEmpty;

	// some parameters are set based on the Evaluator version number
	int nSetWidth=60/nFiles;
	char cCoeffSet=fnBase.end()[-1];


	// read in sets
	nSets=0;
	for (iFile=0; iFile<nFiles; iFile++) {
		FILE* fp;
		std::string fn;
		int iVersion;

		// get file name
		std::ostringstream os;
		os << fnBase << char('a'+(iFile%nFiles)) << ".cof";
		fn=os.str();

		// open file
		fp=fopen(fn.c_str(),"rb");
		if (!fp)
			throw std::string("Can't open coefficient file ")+fn;

		// read in version and parameter info
		u4 fParams;
		fread(&iVersion, sizeof(iVersion), 1, fp);
		fread(&fParams, sizeof(fParams), 1, fp);
		if (iVersion==1 && fParams==14) {
			ConvertFile(fp, fn, iVersion, fParams);
		}
		if (iVersion!=1 || (fParams!=100)) {
			throw std::string("error reading from coefficients file ")+fnBase;
		}

		// calculate the number of subsets
		nSubsets=2;

		for (iSubset=0; iSubset<nSubsets; iSubset++) {
			// allocate memory for the black and white versions of the coefficients
			coeffs[nSets]=new TCoeff[nCoeffsJ];
			CHECKNEW(coeffs[nSets]);

			// put the coefficients in the proper place
			for (map=0; map<nMapsJ; map++) {

				// inital calculations
				nIDs=mapsJ[map].NIDs();
				nConfigs=mapsJ[map].NConfigs();
				mapsize=mapsJ[map].size;
				coeffStart=coeffStartsJ[map];

				// get raw coefficients from file
				i2Coeffs=new i2[nIDs];
				CHECKNEW(i2Coeffs!=NULL);
				if (fread(i2Coeffs,sizeof(u2),nIDs,fp)<size_t(nIDs))
					throw std::string("error reading from coefficients file")+fnBase;

				// convert raw coefficients[id] to i2s[config]
				for (config=0; config<nConfigs; config++) {
					id=mapsJ[map].ConfigToID(config);

					coeff = i2Coeffs[id];

					cid=config+coeffStart;

					if (map==PARJ) {
						// odd-even correction, only in Parity coefficient.
						if (cCoeffSet>='A') {
							if (iFile>=7)
								coeff+=TCoeff(kStoneValue*.65);
							else if (iFile==6)
								coeff+=TCoeff(kStoneValue*.33);
						}
					}
					
					// coeff value has first 2 bytes=coeff, 3rd byte=potmob1, 4th byte=potmob2
					if (map<M1J) {	// pattern maps
						// restrict the coefficient to 2 bytes
						if (coeff>0x3FFF)
							packedCoeff=0x3FFF;
						if (coeff<-0x3FFF)
							packedCoeff=-0x3FFF;
						else
							packedCoeff=coeff;
						
						// get linear pot mob info
						if (map<=D5J) {	// straight-line maps
							fHasPotMobs=true;
							configpm1=configToPotMob[0][mapsize][config];
							configpm2=configToPotMob[1][mapsize][config];
						}
						
						else if (map==C4J) {	// corner triangle maps
							fHasPotMobs=true;
							configpm1=configToPotMobTriangle[0][config];
							configpm2=configToPotMobTriangle[1][config];
						}

						else {	// 2x4, 2x5, edge+2X
							fHasPotMobs=false;
						}

						// pack coefficient and pot mob together
						if (fHasPotMobs)
							packedCoeff=(packedCoeff<<16) | (configpm1<<8) | (configpm2);						

						coeffs[nSets][cid]=packedCoeff;
					}
					
					else {		// non-pattern maps
						coeffs[nSets][cid]=coeff;
					}
				}

				delete[] rawCoeffs;
				delete[] i2Coeffs;
			}

			// fold 2x4 corners into 2x5 corners
			TCoeff* pcf2x4, *pcf2x5;
			TConfig c2x4;

            pcf2x4=&(coeffs[nSets][coeffStartsJ[C2x4J]]);
            pcf2x5=&(coeffs[nSets][coeffStartsJ[C2x5J]]);
            // fold coefficients in
            for (config=0; config<9*6561; config++) {
                c2x4=configs2x5To2x4[config];
                pcf2x5[config]+=pcf2x4[c2x4];
            }
            // zero out 2x4 coefficients
            for (config=0;config<6561; config++) {
                pcf2x4[config]=0;
            }

			// Set the pcoeffs array and the fParameters
			for (nEmpty=59-nSetWidth*iFile; nEmpty>=50-nSetWidth*iFile; nEmpty--) {
				// if this is a set of the wrong parity, do nothing
				if ((nEmpty&1)==iSubset)
					continue;
                pcoeffs[nEmpty]=coeffs[nSets];
            }

			nSets++;
		}
		fclose(fp);
	}
}

CEvaluator::~CEvaluator() {
	int color, set;

	// delete the coeffs array
	for (set=0; set<nSets; set++) {
        delete[] coeffs[set];
	}
}

////////////////////////////////////////
// J evaluation
////////////////////////////////////////

// iDebugEval prints out debugging information in the static evaluation routine.
//	0 - none
//	1 - final value
//	2 - board, final value and all components
// only works with OLD_EVAL set to 1 (slower old version)
const int iDebugEval=0;

inline TCoeff ConfigValue(const TCoeff* pcmove, TConfig config, int map, int offset) {
	TCoeff value=pcmove[config+offset];
	if (iDebugEval>1)
		printf("Config: %5u, Id: %5hu, Value: %4d\n", config, mapsJ[map].ConfigToID(u2(config)), value);
	return value;
}

inline TCoeff PatternValue(TConfig configs[], const TCoeff* pcmove, int pattern, int map, int offset) {
	TConfig config=configs[pattern];
	TCoeff value=pcmove[config+offset];
	if (iDebugEval>1)
		printf("Pattern: %2d - Config: %5u, Id: %5hu, Value: %4d (pms %2d, %2d)\n",
				pattern, config, mapsJ[map].ConfigToID(u2(config)), value>>16, (value>>8)&0xFF, value&0xFF);
	return value;
}

inline TCoeff ConfigPMValue(const TCoeff* pcmove, TConfig config, int map, int offset) {
	TCoeff value=pcmove[config+offset];
	if (iDebugEval>1)
		printf("Config: %5u, Id: %5hu, Value: %4d (pms %2d, %2d)\n",
				config, mapsJ[map].ConfigToID(u2(config)), value>>16, (value>>8)&0xFF, value&0xFF);
	return value;
}


// offsetJs for coefficients
const int offsetJR1=0, sizeJR1=6561,
	offsetJR2=offsetJR1+sizeJR1, sizeJR2=6561,
	offsetJR3=offsetJR2+sizeJR2, sizeJR3=6561,
	offsetJR4=offsetJR3+sizeJR3, sizeJR4=6561,
	offsetJD8=offsetJR4+sizeJR4, sizeJD8=6561,
	offsetJD7=offsetJD8+sizeJD8, sizeJD7=2187,
	offsetJD6=offsetJD7+sizeJD7, sizeJD6= 729,
	offsetJD5=offsetJD6+sizeJD6, sizeJD5= 243,
	offsetJTriangle=offsetJD5+sizeJD5, sizeJTriangle=  9*6561,
	offsetJC4=offsetJTriangle+sizeJTriangle, sizeJC4=6561,
	offsetJC5=offsetJC4+sizeJC4, sizeJC5=6561*9,
	offsetJEX=offsetJC5+sizeJC5, sizeJEX=6561*9,
	offsetJMP=offsetJEX+sizeJEX, sizeJMP=64,
	offsetJMO=offsetJMP+sizeJMP, sizeJMO=64,
	offsetJPMP=offsetJMO+sizeJMO, sizeJPMP=64,
	offsetJPMO=offsetJPMP+sizeJPMP, sizeJPMO=64,
	offsetJPAR=offsetJPMO+sizeJPMO, sizeJPAR=2;

// value all the edge patterns. return the sum of the values.
static TCoeff ValueEdgePatternsJ(const TCoeff* pcmove, TConfig config1, TConfig config2) {
    TConfig configXX;
	u4 configs2x5;
	TCoeff value;

	value=0;

    configs2x5=row1To2x5[config1]+row2To2x5[config2];
	configXX=config1+(config1<<1)+row2ToXX[config2];
	value+=ConfigValue(pcmove, configs2x5&0xFFFF, C2x5J, offsetJC5);
	value+=ConfigValue(pcmove, configs2x5>>16,    C2x5J, offsetJC5);
	value+=ConfigValue(pcmove, config1+(config1<<1)+row2ToXX[config2],CR1XXJ, offsetJEX);

	// in J-configs, the values are multiplied by 65536
	//assert((value&0xFFFF)==0);
	return value;
}

// value all the triangle patterns. return the sum of the values.
static TCoeff ValueTrianglePatternsJ(const TCoeff* pcmove, TConfig config1, TConfig config2, TConfig config3, TConfig config4) {
	u4 configsTriangle;
	TCoeff value;

	value=0;

	configsTriangle=row1ToTriangle[config1]+row2ToTriangle[config2]+row3ToTriangle[config3]+row4ToTriangle[config4];
	value+=ConfigPMValue(pcmove, configsTriangle&0xFFFF, C4J, offsetJTriangle);
	value+=ConfigPMValue(pcmove, configsTriangle>>16,    C4J, offsetJTriangle);

	return value;
}

static CValue ValueJMobs(const CBitBoard &bb, int nEmpty, bool fBlackMove, TCoeff *const pcoeffs, u4 nMovesPlayer, u4 nMovesOpponent) {
	TCoeff value = 0;

	if (iDebugEval>1) {
        cout << "----------------------------\n";
		//bb.Print(fBlackMove);
		cout << (fBlackMove?"Black":"White") << " to move\n\n";
	}

    uint64_t empty = bb.empty;
    uint64_t mover = bb.mover;

    // EXTRACT_BITS_U64 takes four parameters:
    // base value
    // start bit (constant)
    // count (constant)
    // step (constant)

#define BB_EXTRACT_STEP_PATTERN(START, COUNT, STEP) \
    base2ToBase3Table[EXTRACT_BITS_U64(empty, (START), (COUNT), (STEP))] + \
    base2ToBase3Table[EXTRACT_BITS_U64(mover, (START), (COUNT), (STEP))] * 2

	const TCoeff* const pR1 = pcoeffs+offsetJR1;
	const TCoeff* const pR2 = pcoeffs+offsetJR2;
	const TCoeff* const pR3 = pcoeffs+offsetJR3;
	const TCoeff* const pR4 = pcoeffs+offsetJR4;
	const TCoeff* const pD5 = pcoeffs+offsetJD5;
	const TCoeff* const pD6 = pcoeffs+offsetJD6;
	const TCoeff* const pD7 = pcoeffs+offsetJD7;
	const TCoeff* const pD8 = pcoeffs+offsetJD8;

    int16_t Diag8A = BB_EXTRACT_STEP_PATTERN(0, 8, 9);
    value += pD8[Diag8A];
    int16_t Diag8B =
        base2ToBase3Table[EXTRACT_BITS_U64(empty, 7, 4, 7) | (EXTRACT_BITS_U64(empty, 35, 4, 7) << 4)] +
        base2ToBase3Table[EXTRACT_BITS_U64(mover, 7, 4, 7) | (EXTRACT_BITS_U64(mover, 35, 4, 7) << 4)] * 2;
    value += pD8[Diag8B]; 

    int16_t Diag7A1 = BB_EXTRACT_STEP_PATTERN(1, 7, 9);
    value += pD7[Diag7A1];
    int16_t Diag7A2 = BB_EXTRACT_STEP_PATTERN(8, 7, 9);
    value += pD7[Diag7A2];
    int16_t Diag7B1 = BB_EXTRACT_STEP_PATTERN(6, 7, 7);
    value += pD7[Diag7B1];
    int16_t Diag7B2 = BB_EXTRACT_STEP_PATTERN(15, 7, 7);
    value += pD7[Diag7B2];

    int16_t Diag6A1 = BB_EXTRACT_STEP_PATTERN(2, 6, 9);
    value += pD6[Diag6A1];
    int16_t Diag6A2 = BB_EXTRACT_STEP_PATTERN(16, 6, 9);
    value += pD6[Diag6A2];
    int16_t Diag6B1 = BB_EXTRACT_STEP_PATTERN(5, 6, 7);
    value += pD6[Diag6B1];
    int16_t Diag6B2 = BB_EXTRACT_STEP_PATTERN(23, 6, 7);
    value += pD6[Diag6B2];

    int16_t Diag5A1 = BB_EXTRACT_STEP_PATTERN(3, 5, 9);
    value += pD5[Diag5A1];
    int16_t Diag5A2 = BB_EXTRACT_STEP_PATTERN(24, 5, 9);
    value += pD5[Diag5A2];
    int16_t Diag5B1 = BB_EXTRACT_STEP_PATTERN(4, 5, 7);
    value += pD5[Diag5B1];
    int16_t Diag5B2 = BB_EXTRACT_STEP_PATTERN(31, 5, 7);
    value += pD5[Diag5B2];

	
    int16_t Column0 = BB_EXTRACT_STEP_PATTERN(0, 8, 8);
    value += pR1[Column0];
    int16_t Column7 = BB_EXTRACT_STEP_PATTERN(7, 8, 8);
    value += pR1[Column7];
    int16_t Column1 = BB_EXTRACT_STEP_PATTERN(1, 8, 8);
    value += pR2[Column1];
    int16_t Column6 = BB_EXTRACT_STEP_PATTERN(6, 8, 8);
    value += pR2[Column6];
    int16_t Column2 = BB_EXTRACT_STEP_PATTERN(2, 8, 8);
    value += pR3[Column2];
    int16_t Column5 = BB_EXTRACT_STEP_PATTERN(5, 8, 8);
    value += pR3[Column5];
    int16_t Column3 = BB_EXTRACT_STEP_PATTERN(3, 8, 8);
    value += pR4[Column3];
    int16_t Column4 = BB_EXTRACT_STEP_PATTERN(4, 8, 8);
    value += pR4[Column4];
#undef BB_EXTRACT_STEP_PATTERN


#define BB_EXTRACT_ROW_PATTERN(ROW) \
    base2ToBase3Table[(empty >> (8 * (ROW))) & 0xff] + \
    base2ToBase3Table[(mover >> (8 * (ROW))) & 0xff] * 2

    int16_t Row0 = BB_EXTRACT_ROW_PATTERN(0);
    value += pR1[Row0];
    int16_t Row1 = BB_EXTRACT_ROW_PATTERN(1);
    value += pR2[Row1];
    int16_t Row2 = BB_EXTRACT_ROW_PATTERN(2);
    value += pR3[Row2];
    int16_t Row3 = BB_EXTRACT_ROW_PATTERN(3);
    value += pR4[Row3];

    int16_t Row4 = BB_EXTRACT_ROW_PATTERN(4);
    value += pR4[Row4];
    int16_t Row5 = BB_EXTRACT_ROW_PATTERN(5);
    value += pR3[Row5];
    int16_t Row6 = BB_EXTRACT_ROW_PATTERN(6);
    value += pR2[Row6];
    int16_t Row7 = BB_EXTRACT_ROW_PATTERN(7);
    value += pR1[Row7];
#undef BB_EXTRACT_ROW_PATTERN

	if (iDebugEval>1)
		printf("Straight lines done. Value so far: %d.\n",value>>16);

	// Triangle patterns in corners
    value += ValueTrianglePatternsJ(pcoeffs, Row0, Row1, Row2, Row3);
    value += ValueTrianglePatternsJ(pcoeffs, Row7, Row6, Row5, Row4);

	if (iDebugEval>1)
		printf("Corners done. Value so far: %d.\n",value);


	// Take apart packed information about pot mobilities
	int nPMO=(value>>8) & 0xFF;
	int nPMP=value&0xFF;
	if (iDebugEval>1)
		printf("Raw pot mobs: %d, %d\n", nPMO,nPMP);
	nPMO=(nPMO+potMobAdd)>>potMobShift;
	nPMP=(nPMP+potMobAdd)>>potMobShift;
	value>>=16;

	// pot mobility
	value += ConfigValue(pcoeffs, nPMP, PM1J, offsetJPMP);
	value += ConfigValue(pcoeffs, nPMO, PM2J, offsetJPMO);

	if (iDebugEval>1)
		printf("Potential mobility done. Value so far: %d.\n",value);

    value += ValueEdgePatternsJ(pcoeffs, Row0, Row1);
    value += ValueEdgePatternsJ(pcoeffs, Row7, Row6);
    value += ValueEdgePatternsJ(pcoeffs, Column0, Column1);
    value += ValueEdgePatternsJ(pcoeffs, Column7, Column6);

	if (iDebugEval>1)
		printf("Edge patterns done. Value so far: %d.\n",value);


	// mobility
	value+=ConfigValue(pcoeffs, nMovesPlayer, M1J, offsetJMP);
	value+=ConfigValue(pcoeffs, nMovesOpponent, M2J, offsetJMO);

	if (iDebugEval>1)
		printf("Mobility done. Value so far: %d.\n",value);

	// parity
	value+=ConfigValue(pcoeffs, nEmpty&1, PARJ, offsetJPAR);

	if (iDebugEval>0)
		printf("Total Value= %d\n", value);

	return CValue(value);
}

// pos2 evaluators
CValue CEvaluator::EvalMobs(const Pos2& pos2, u4 nMovesPlayer, u4 nMovesOpponent) const {
	int nEmpty = pos2.NEmpty();
    return ValueJMobs(pos2.GetBB(), nEmpty, pos2.BlackMove(), pcoeffs[nEmpty], nMovesPlayer, nMovesOpponent);
}
