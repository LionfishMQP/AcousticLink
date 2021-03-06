#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

//  global
#define maxSampleLength 100000
double samples[maxSampleLength];
double times[maxSampleLength];

double filteredSamples[maxSampleLength];

double FIRB[] = {0.000000,0.010007,0.025102,0.030527,-0.000000,-0.067929,-0.126095,-0.109084,-0.000000,0.136015,0.197597,0.136015,-0.000000,-0.109084,-0.126095,-0.067929,-0.000000,0.030527,0.025102,0.010007,0.000000};

//  local 
#define MAX_LINE_SIZE 100



/**
 * reads sample data from CSV file. 
 * param: char array for fileName used in fopen
 * return actual length of data points read into array. return -1 for faile to open file
**/
int32_t readSamplesFromCsv( char *fName )
{
	FILE *fp;
	fp = fopen(fName,"r");
	if (fp == NULL)
	{
		printf("%s file doesn't exist", fName);
		return -1;
	}
	
	int32_t i=0;
	// for(; i< maxSampleLength ; ++i)
	for(; i<maxSampleLength ; ++i)
	{
		if( fscanf(fp, "%lf,%lf" , &times[i], &samples[i] )== EOF )
		{
			break;
		}

		printf("time: %lf sample: %lf \n ,",times[i],samples[i] );
	}
	fclose (fp);
	return i;
}


/**
 * write the content in filteredSamples into csv 
 * param: fName, name of the file to write to, length: length of the sample. 
 * return -1 if file open failed. 
 * **/
int32_t writeResultToCsv( char *fName , int32_t length)
{
	FILE *fp;
	fp = fopen(fName,"w+");
	if (fp == NULL)
	{
		printf("%s file doesn't exist", fName);
		return -1;
	}

	for(int i=0;i<length;++i)
	{
		fprintf(fp , "%f,%f\n" ,times[i], filteredSamples[i]);
	}
	fclose(fp);
	return 0;

}


/**
 * use coeffs in FIRB to fir filter the data in sample and store into filteredsamples
 * param: maxLength: maxlength of the data set in sample array
 *
 **/
void FIRsamples(uint32_t maxLength)
{
	int	Blength = sizeof(FIRB)/sizeof(FIRB[0]) ; 
	if (maxLength>maxSampleLength)
	{
		maxLength = maxSampleLength;
	}


	for(int i = Blength -1; i<maxLength ; ++i )
	{
		// process each data point, in samples[i] and store into filteredSamples[i]
		filteredSamples[i]=0; // clear the data in result 
		for (int j =0; j<Blength ; ++j)
		{	
			filteredSamples[i] += FIRB[j] * samples[i-j]; // this pair xn to b0, xn-j to bj
		}
	}
}

int main()
{
	printf("main.c");
	char finName[] = "modulated_binary.csv";
	char foutName[] = "processedSamples.csv";
	// read in sample points
	int32_t length = readSamplesFromCsv(finName);	

	// process sample points
	FIRsamples(length);

	writeResultToCsv(foutName,length);
}