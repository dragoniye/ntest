// Copyright Chris Welty
//	All Rights Reserved
// This file is distributed subject to GNU GPL version 2. See the files
// Copying.txt and GPL.txt for details.

// source file for options

#include "n64/utils.h"
#include "options.h"
#ifdef _WIN32
#include "windows.h"
#endif

int treeNEmpty;

// opponent's move?
bool fTooting=false;
bool fMyMove=false;

// book-do solved nodes count as minimal nodes?
bool fSolvedAreMinimal=true;

bool fPrintAbort=true;

#ifdef _DEBUG
bool fPrintTree=false;
#endif
bool fPrintWLD=false;
bool fCompareMode=false;
bool fInTournament=false;

//Captured positions
FILE* cpFile;
int nCapturedPositions=0;

int hNegascout=6;

