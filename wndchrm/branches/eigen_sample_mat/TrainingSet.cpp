/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*                                                                               */
/*    Copyright (C) 2007 Open Microscopy Environment                             */
/*         Massachusetts Institue of Technology,                                 */
/*         National Institutes of Health,                                        */
/*         University of Dundee                                                  */
/*                                                                               */
/*                                                                               */
/*                                                                               */
/*    This library is free software; you can redistribute it and/or              */
/*    modify it under the terms of the GNU Lesser General Public                 */
/*    License as published by the Free Software Foundation; either               */
/*    version 2.1 of the License, or (at your option) any later version.         */
/*                                                                               */
/*    This library is distributed in the hope that it will be useful,            */
/*    but WITHOUT ANY WARRANTY; without even the implied warranty of             */
/*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU          */
/*    Lesser General Public License for more details.                            */
/*                                                                               */
/*    You should have received a copy of the GNU Lesser General Public           */
/*    License along with this library; if not, write to the Free Software        */
/*    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA  */
/*                                                                               */
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*                                                                               */
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/* Written by:  Lior Shamir <shamirl [at] mail [dot] nih [dot] gov>              */
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#ifdef WIN32
#pragma hdrstop
#endif
#include <algorithm>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cfloat> // Has definition of DBL_EPSILON
#include <cmath>
#include <ctime>
#include "FeatureNames.hpp"
#include "wndchrm_error.h"

//#include <limits>


#include "TrainingSet.h"

#include "gsl/specfunc.h"
#include "MAP.h"
//#include "cmatrix.h"

#ifndef WIN32
#include <stdlib.h>
#else
#include <dir.h>
#endif

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h> // unlink

#define FLOAT_EQ(x,v,y) (((v - FLT_EPSILON * y) < x) && (x < ( v + FLT_EPSILON * y)))

#define DEBUG_CREATE_INDIV_DISTANCE_FILES 0

/* global variable */
extern int verbosity;

/* compare_two_doubles
   function used for qsort
*/
int compare_two_doubles (const void *a, const void *b)
{
  if (*((double *)a) > *((double*)b)) return(1);
  if (*((double*)a) == *((double*)b)) return(0);
  return(-1);
}

int comp_strings(const void *s1, const void *s2)
{  return(strcmp((char *)s1,(char *)s2));
}

/* check if the file format is supported */
int IsSupportedFormat(char *filename) {
	char *char_p;
	
	char_p = strrchr (filename,'.');
	if (!char_p) return (0);
	
	if (!strcmp(char_p,".sig")) return(1);  /* ignore files the extension but are actually .sig files */
#ifdef WIN32	  
	if (!strcmp(char_p,".bmp") || !strcmp(char_p,".BMP")) return(1);
#endif  
	if (!strcmp(char_p,".tif") || !strcmp(char_p,".TIF") || !strcmp(char_p,".tiff") || !strcmp(char_p,".TIFF") || !strcmp(char_p,".ppm") || !strcmp(char_p,".PPM")) return(1);  /* process only image files */
	if (!strcmp(char_p,".dcm") || !strcmp(char_p,".DCM")) return(1);
  return(0);
}

//---------------------------------------------------------------------------

/* constructor of a TrainingSet object
   samples_num -long- a maximal number of samples in the training set
*/
TrainingSet::TrainingSet(long samples_num, long class_num) {
	int indx;

   /* initialize */
	for (indx = 0;indx < MAX_SIGNATURE_NUM; indx++) {
		SignatureNames[indx][0]='\0';
	}
	this->class_num=0;
	color_features=0;     /* by default - no color features are used */
	signature_count=0;
	is_continuous=0;
	is_numeric=0;
	is_pure_numeric=0;

	count=0;
// Add sample matrixes for the 0 class
	Eigen::MatrixXd newMatrix;
	raw_features.push_back (newMatrix);
	projected_features.push_back (newMatrix);

	class_labels.push_back (UNKNOWN_CLASS_LABEL);
	class_nsamples.push_back (0);

	train_class = NULL;

	std::vector<signatures *> class_sample_vec;
	class_samples.push_back (class_sample_vec);

	// Memory allocated for the aggregated_feature_stats
	// if this* is the top Level TrainingSet from which all
	// splits are derived
	aggregated_feature_stats = NULL;

}

/* destructor of a training set object
*/
TrainingSet::~TrainingSet() {
	int indx;
	for (indx=0;indx<count;indx++)
		if (samples[indx]) delete samples[indx];
	for (indx=class_num;indx>=0;indx--) {
		if ((int)raw_features.size() > indx) {
			raw_features.pop_back();
		}
		if ((int)projected_features.size() > indx) {
			projected_features.pop_back();
		}
	}
	if( train_class ) delete train_class;
}


/* AddContinuousClass
   Add a class to the training set to contain continuous value samples (not discrete)
   
   label -char *- class label - can be NULL or pointer to NULL byte.
   returned value -int- The index of the class.

   Currently it is an error to add a continuous class to a dataset that already has discrete classes
*/
int TrainingSet::AddContinuousClass(char *label) {

// Otherwise, its a warning for make_continuous to be 1 while pure_numeric is false.
	if (class_num > 0 && !is_continuous) {
		catError ("WARNING: Software error (bug): Making a continuous dataset when there are discrete classes already defined. Keeping discrete classes\n");
		is_continuous = 0;
		return (CONTINUOUS_DATASET_WITH_CLASSES);
	} else if (is_continuous && class_labels[CONTINUOUS_CLASS_INDEX].compare(label)) {
		catError ("WARNING: Software error (bug): Adding a second continuous class to a continuous dataset is not allowed. Ignoring second continuous class\n");
		is_continuous = 1;
		return (CONTINUOUS_CLASS_INDEX);
	}

	is_continuous = 1;
	is_numeric = 1;
	class_num = 1;
	if (label) class_labels[CONTINUOUS_CLASS_INDEX] = label;
	else class_labels[CONTINUOUS_CLASS_INDEX].clear();
	class_nsamples[CONTINUOUS_CLASS_INDEX] = 0;
	
	return (1);
}


/* AddClass
   Add a class to the training set
   
   label -char *- class label - can be NULL or pointer to NULL byte for unknown class.
   returned value -int- The index of the class.  If the class label already exists, no new class is added.
   
   The class labels are unique, and are stored in sort order.
   If the class label is new, and is out of sort-order, an error (<0) will be returned.  
   It is also an error to call this if is_numeric is true
*/
int TrainingSet::AddClass(const char *label) {
int cmp_label;
int numeric;

// Check if we're adding to a continuous dataset
	if (is_continuous && (label || *label)) {
		catError ("Error adding class '%s': Can't add classes to a continuous dataset.\n",label);
		return (ADDING_CLASS_TO_CONTINUOUS_DATASET);
	} else if (is_continuous) {
		return (CONTINUOUS_CLASS_INDEX);
	}

// Only add the class if its in sort order
	if (class_labels.size() > 0) {
		cmp_label = -class_labels[class_num].compare (label);
	} else if (*label) {
		cmp_label = 1;
	} else {
		cmp_label = 0;
	}

// if it already exists, return class_num
	if (cmp_label == 0) {
		return (class_num);

// If it belongs at the end of the ordered class list, add it.
	} else if (cmp_label > 0) {
		// Check if we have too many classes
		if (class_num >= MAX_CLASS_NUM-1) {
			catError ("Maximum number of classes (%d) exceeded.\n",MAX_CLASS_NUM-1);
			return (TOO_MANY_CLASSES);
		}

		class_num++;
		class_labels.push_back (label);
		class_nsamples.push_back (0);
		std::vector<signatures *> class_sample_vec;
		class_samples.push_back (class_sample_vec);

	// Add sample matrixes for this class
		if ((int)raw_features.size() < class_num+1) {
			Eigen::MatrixXd newMatrix;
			raw_features.push_back (newMatrix);
			projected_features.push_back (newMatrix);
		}

		// Check if its numeric.  If not, global is_numeric set to false.
		numeric = check_numeric(class_labels[class_num].c_str(),NULL);
		if (numeric && class_num == 1) { //you only get to turn on numeric if you're the first class being read in
			is_numeric = 1;
			if (numeric == 2) is_pure_numeric = 1;
		} else if (class_num > 1) { // after that, you only get to turn it off
			if (!numeric) {is_numeric = 0; is_pure_numeric = 0;}
			else if (numeric == 1) is_pure_numeric = 0;
		}

		return (class_num);

// If its being added out of order, its an error (software error, not user error).
	} else {
		catError ("Adding class '%s' out of sort order (%d classes, last class = '%s').\n",label,class_num,class_labels[class_num].c_str());
		return (CANT_ADD_UNORDERED_CLASS);
	}

}

/* AddSample
   Add the signatures computed from one image to the training set
   new_sample -signatures- the set of signature values
   path -char *- full path to the image file (NULL if n/a)

   returned value -int- 1 if suceeded 0 if failed.
                        can fail due to bad sample class
*/
int TrainingSet::AddSample(signatures *new_sample)
{
	int sample_class = new_sample->sample_class;
   /* check if the sample can be added */
	if (sample_class > class_num) {
		errno = 0;
		catError ("Adding sample with class index %d, but only %d classes defined.\n",sample_class,class_num);
		return (ADDING_SAMPLE_TO_UNDEFINED_CLASS);
	}
	if (signature_count > 0)
		signature_count = new_sample->count;


// Add the sample to the samples vector
	samples.push_back (new_sample);
	class_samples[sample_class].push_back (new_sample);
	signature_count=new_sample->count;
	class_nsamples[sample_class]++;


// Add a column to our sample matrix
	int col = class_nsamples[sample_class]-1;
	Eigen::MatrixXd &raw_features_ref = raw_features[sample_class];
	if (raw_features_ref.cols() < col+1) {
		raw_features_ref.conservativeResize(signature_count, col+1);
		if (raw_features_ref.cols() < col) {
			catError ("Can't add column to feature matrix.\n");
			return (CANT_ALLOCATE_MEMORY);
		} else {
			errno = 0;
		}
	}
// copy the values out of the sample into the last column of the matrix
	for (int i = 0; i < signature_count; i++) {
		raw_features_ref(i,col) = new_sample->data[i].value;
	}
// Tell the sample where it is in matrix format (and have it clean up temporary storage)
	new_sample->Finalize (raw_features_ref,col);

//printf ("Adding Sample to class: %d, total:%ld, signature_count:%ld\n",sample_class,class_nsamples[sample_class],signature_count);
	count++;
	return(1);
}

/* SaveToFile
   filename -char *- the name of the file to save
   returned value -int- 1 is successful, 0 if failed.

   comment: saves the training set into a text file
*/
int TrainingSet::SaveToFile(char *filename)
{  int sample_index, class_index, sig_index;
   FILE *file;
   if (!(file=fopen(filename,"w"))) {
   	catError ("Couldn't open '%s' for writing.\n");
   	return(0);
   }
   fprintf(file,"%ld\n",class_num);
   fprintf(file,"%ld\n",signature_count);
   fprintf(file,"%ld\n",count);
   /* write the signature names */
   for (sig_index=0;sig_index<signature_count;sig_index++)
     fprintf(file,"%s\n",SignatureNames[sig_index]);
   /* write the class labels */
   for (class_index=0;class_index<=class_num;class_index++)
     fprintf(file,"%s\n",class_labels[class_index].c_str());
   /* write the samples */
   for (sample_index=0;sample_index<count;sample_index++)
   {
      for (sig_index=0;sig_index<signature_count;sig_index++)
        if (samples[sample_index]->data[sig_index].value==(int)(samples[sample_index]->data[sig_index].value))
      fprintf(file,"%ld ",(long)(samples[sample_index]->data[sig_index].value));      /* make the file smaller */
//        else fprintf(file,"%.6f ",samples[sample_index]->data[sig_index].value);
      else fprintf(file,"%.5e ",samples[sample_index]->data[sig_index].value);
      if (is_continuous) fprintf(file,"%f\n",samples[sample_index]->sample_value);  /* if the class is 0, save the continouos value of the sample */
	  else fprintf(file,"%d\n",samples[sample_index]->sample_class);   /* save the class of the sample */
      fprintf(file,"%s\n",samples[sample_index]->full_path);
   }
   fclose(file);
   return(1);
}

/* IsFitFile
   filename -char *- the name of the file to open
   returned value -int- 1 is a .fit file, 0 if not.

   comment: opens the file and checks if the first three lines are "pure numeric"
*/
bool TrainingSet::IsFitFile(char *filename) {
	char buffer[128];
	FILE *file;
	int line_num=3; // number of pure numeric lines to check

	if (!(file=fopen(filename,"r"))) return (0);
	while (line_num) {
		fgets(buffer,sizeof(buffer),file);
		chomp (buffer);
		if (check_numeric (buffer,NULL) != 2) break;
		line_num--;
	}
	fclose (file);
	return (line_num == 0);
}


/* ReadFromFile
   filename -char *- the name of the file to open
   returned value -int- 1 is successful, 0 if failed.

   comment: reads the training set from a text file
*/
int TrainingSet::ReadFromFile(char *filename)
{  int sample_index, class_index, sample_count,sig_index;
   int res, file_class_num;
   char buffer[50000];
   FILE *file;
   if (!(file=fopen(filename,"r"))) {
    catError ("Can't read .fit file '%s'\n",filename);
   	return(CANT_OPEN_FIT);
   }

   for (sample_index=0;sample_index<count;sample_index++)
     if (samples[sample_index]) delete samples[sample_index];

   fgets(buffer,sizeof(buffer),file);
   file_class_num=atoi(buffer);
   fgets(buffer,sizeof(buffer),file);
   signature_count=atoi(buffer);
   fgets(buffer,sizeof(buffer),file);
   sample_count=atoi(buffer);
   count=0;         /* initialize the count before adding the samples to the training set */
   color_features=0;
   /* read the signature names */
   for (sig_index=0;sig_index<signature_count;sig_index++)
   {  fgets(buffer,sizeof(buffer),file);
      chomp (buffer);
      strcpy(SignatureNames[sig_index],buffer);
      if (strstr(SignatureNames[sig_index],"color") || strstr(SignatureNames[sig_index],"Color")) color_features=1;   /* check if color signatures are used */
   }

// skip blank lines
	*buffer = '\0';
	while (*buffer == '\0') {
		fgets(buffer,sizeof(buffer),file);
		chomp (buffer);
	}

   /* read the class labels */
	for (class_index=0;class_index < file_class_num;class_index++) {  
		if ( (res = AddClass(buffer) < 0) ) {
			fclose(file);
			return (res);
		}
		fgets(buffer,sizeof(buffer),file);
		chomp (buffer);
	}

   /* read the samples */
   for (sample_index=0;sample_index<sample_count;sample_index++)
   {  char *p_buffer;
      signatures *one_sample;
      one_sample=new signatures();
      p_buffer=strtok(buffer," \n");
      for (sig_index=0;sig_index<signature_count;sig_index++)
      {  one_sample->Add(SignatureNames[sig_index],atof(p_buffer));
         p_buffer=strtok(NULL," \n");
      }
      one_sample->sample_class=atoi(p_buffer);                  /* read the class of the sample                     */
      if (is_continuous) one_sample->sample_value=atof(p_buffer);/* use the same value as an continouos value        */
      else one_sample->sample_value=atof(class_labels[one_sample->sample_class].c_str()); /* use the class label as a value */
      fgets(buffer,sizeof(buffer),file);                        /* read the image path (can also be en ampty line)  */
      chomp (buffer);
      strcpy(one_sample->full_path,buffer);                     /* copy the full path to the signatures object      */
      if ( (res=AddSample(one_sample)) < 0) {
        for (sig_index=0;sig_index<sample_index;sig_index++) delete samples[sig_index];
      	fclose(file);
      	return (res);
      }
      fgets(buffer,sizeof(buffer),file);
   }

   fclose(file);

   return(1);
}

/*
  MakeContinuous
  Make an existing TrainingSet (with defined classes and samples) into a continuous TrainingSet
*/
void TrainingSet::MakeContinuous(char *label) {
long index;

	for (index=0;index < class_num;index++) class_labels[index].clear();
	if (label) class_labels[CONTINUOUS_CLASS_INDEX] = label;
	else class_labels[CONTINUOUS_CLASS_INDEX].clear();


	/* make the samples referring to class_index refer to class 0 */
	// And samples with index greater than class_index refer to an index lower by 1.
	for (index=0;index<count;index++) if (samples[index]->sample_class) samples[index]->sample_class=CONTINUOUS_CLASS_INDEX;
	
	// The number of defined classes is reduced by 1
	class_num=1;
	is_continuous = 1;
}

/*
  MarkUnknown
  mark an existing class and its samples as unknown (class 0).
*/
void TrainingSet::MarkUnknown(long class_index) {
long index;

	if (class_index > class_num || class_index == 0) return;
	/* move the class labels at and above class_index */
	class_nsamples[0] += class_nsamples[class_index];
	for (index=class_index;index < class_num;index++) {
		class_labels[index] = class_labels[index+1];
		class_nsamples[index] = class_nsamples[index+1];
	}

	/* make the samples referring to class_index refer to class 0 */
	// And samples with index greater than class_index refer to an index lower by 1.
	for (index=0;index<count;index++) {
		if (samples[index]->sample_class==class_index) {
			samples[index]->sample_class = 0;
		} else if (samples[index]->sample_class > class_index) {
			samples[index]->sample_class--;
		}
	}
	
	// The number of defined classes is reduced by 1
	class_num--;
}

/* RemoveClass
   remove a class from the training set
   class_index -long- the index of the class to be removed
   The effect of removing class index 0 is to remove the samples of that class, but not
   change the indexes of the other samples.
*/
void TrainingSet::RemoveClass(long class_index)
{  long index,deleted_count=0;

	if (class_index >= class_num || class_index < 0) return;
// remove the class label , and shift labels down only for index > 0
	if (class_index > 0) {
		for (index=class_index;index<class_num;index++) {
			class_labels[index] = class_labels[index+1];
			class_nsamples[index] = class_nsamples[index+1];
		}
	} else {
		class_nsamples[0] = 0;
	}
	
// remove the samples of that class
	for (index=0;index<count;index++)  {
		if (samples[index]->sample_class==class_index) {
			delete samples[index];
			deleted_count++;
		} else samples[index-deleted_count]=samples[index];
	}

// set the new number of samples
	count=count-deleted_count;
// change the indices of the samples, only if class_index > 0
	if (class_index > 0) {
		for (index=0;index<count;index++)
			if (samples[index]->sample_class > class_index)
				samples[index]->sample_class=samples[index]->sample_class-1;
	// change the number of classes
		class_num--;
	}

	return;
}

/* SaveWeightVector
   save the weights of the features into a file 
   filename -char *- the name of the file into which the weight values should be written
*/
int TrainingSet::SaveWeightVector(char *filename)
{  FILE *sig_file;
   int sig_index;
   if (!(sig_file=fopen(filename,"w"))) {
    catError ("Can't write weight vector to '%s'.\n",filename);
   	return(0);
   }
   if (verbosity>=2) printf("Saving weight vector to file '%s'...\n",filename);   
   for (sig_index=0;sig_index<signature_count;sig_index++)
     fprintf(sig_file,"%f %s\n",SignatureWeights[sig_index],SignatureNames[sig_index]);
   fclose(sig_file);
   return(1);
}

/* LoadWeightVector
   load the weights of the features from a file and assign them to the features of the training set
   filename -char *- the name of the file into which the weight values should be read from
   factor -double- multiple the loaded feature vector and add to the existing vecotr (-1 is subtracting). 0 replaces the existing vector with the loaded vector.
   returned value -double- the square difference between the original weight vector and the imported weight vector
*/
double TrainingSet::LoadWeightVector(char *filename, double factor)
{  FILE *sig_file;
   int sig_index=0;
   char line[128],*p_line;
   double feature_weight_distance=0.0;
   if (!(sig_file=fopen(filename,"r"))) {
    catError ("Can't read weight vector from '%s'.\n",filename);
   	return(0);
   }
   if (verbosity>=2) printf("Loading weight vector from file '%s'...\n",filename);
   p_line=fgets(line,sizeof(line),sig_file);
   while (p_line)
   {  if (strlen(p_line)>0)
      {  if (strchr(p_line,' ')) (*strchr(p_line,' '))='\0';
         feature_weight_distance+=pow(SignatureWeights[sig_index]-atof(p_line),2);
         if (factor==0) SignatureWeights[sig_index++]=atof(p_line);
	     else SignatureWeights[sig_index++]+=factor*atof(p_line);
         if (SignatureWeights[sig_index-1]<0) SignatureWeights[sig_index-1]=0;		  
	  }
      p_line=fgets(line,sizeof(line),sig_file);   
   }
   fclose(sig_file);
	if (sig_index!=signature_count) {
		catError ("Feature count in weight vector '%s' (%d) don't match dataset (%d).\n",filename,sig_index,signature_count);
		return(-1.0);
	}
   return(sqrt(feature_weight_distance));
}

/* set attrib
   Set the attributes of a given set
*/
void TrainingSet::SetAttrib(TrainingSet *set)
{  int class_index,sig_index;
   set->signature_count=signature_count;
   set->color_features=color_features;
	 // set->count = count; don't set this, count get incremented as you load sigs into it
   /* copy the signature names to the training and test set */
   for (sig_index=0;sig_index<signature_count;sig_index++)
     strcpy(set->SignatureNames[sig_index],SignatureNames[sig_index]);
   set->is_numeric = is_numeric;
   set->is_pure_numeric = is_pure_numeric;
   set->is_continuous = is_continuous;

   /* copy the class labels to the train and test */
   for (class_index=0;class_index<=class_num;class_index++) {
     set->AddClass( class_labels[class_index].c_str() );
   }
   
}

/*  split
    split into a training set and a test set
    randomize -int- If true, split randomly.  If false, split by sample order in object
    ratio -double- the fraction of images to use for training (e.g., 0.1 means 10% of the data are train data).
                   If the ratio is 0, use train_samples as the number for training
                   If its > 0.0 and <= 1.0, use this fraction of each class for training (allows unbalanced training)
    TrainSet -TrainingSet *- Where to store the training samples after the split.
    TestSet -TrainingSet *- Where to store the test samples after the split.
                   If TestSet->count > 0, then the test set has come from a file, and is left unmodified
    tiles -unsigned short- indicates the number of tiles to which each image was divided into. This means that
                           when splitting to train and test, all tiles of that one image will be either in the
                           test set or training set.
    train_samples -int- the number of samples to use for training (if ratio is 0)
    test_samples -int- the number of samples to use for testing (if TestSet->count == 0)
    N.B.: It is a fatal error for train_samples+test_samples to be greater than the number of images in the class.
    Range-checking must occur outside of this call.
*/
int TrainingSet::split(int randomize, double ratio,TrainingSet *TrainSet,TrainingSet *TestSet, unsigned short tiles, int train_samples, int test_samples, data_split *split)
{
	int class_index;
	int number_of_test_samples, number_of_train_samples;
	bool make_test_set = true;

 	// copy the same attributes to the training and test set
 	SetAttrib( TrainSet );
 
 	// don't change the test set if it pre-exists
 	if( ! TestSet->count > 0 ) SetAttrib(TestSet);
 
 	 // make sure the number of tiles is valid
 	if( tiles < 1 )	tiles = 1;

	TrainSet->class_num = TestSet->class_num = class_num;
 
 	// if test already has samples from a file
 	if( TestSet->count > 0 )	make_test_set = false;
 
 	// iterate over each class	
	for( class_index = 1; class_index <= class_num; class_index++ ) {
		int sample_index;
		int class_samples_count=class_nsamples[class_index];
		class_samples_count /= tiles;

		// Make an array of sample numbers
		std::vector<int> train_test_split (class_samples_count);

		for( sample_index = 0; sample_index < class_samples_count; sample_index++ )
			train_test_split[sample_index] = sample_index;

		// Shuffle it if we're randomizing
		if( randomize )	random_shuffle( train_test_split.begin(), train_test_split.end() );

		// Determine number of training samples.
		if( ratio > 0.0 && ratio <= 1.0 ) { // unbalanced training
			number_of_train_samples = (int)floor( (ratio * (float)class_samples_count) + 0.5 );
 			if (!test_samples && make_test_set) number_of_test_samples = class_samples_count - number_of_train_samples;
 			else if (make_test_set) number_of_test_samples = test_samples;
 			else number_of_test_samples = 0;
 		} else {
			number_of_train_samples = train_samples;
			if (make_test_set) number_of_test_samples = test_samples;
			else number_of_test_samples = 0;
		}

		// add the samples to the training set
		if( number_of_train_samples + number_of_test_samples > class_samples_count ) {
			printf("While splitting class %s, training images (%d) + testing images (%d) is greater than total images in the class (%d)\n",
					class_labels[class_index].c_str(), number_of_train_samples, number_of_test_samples, class_samples_count);
			exit (-1);
		}

		//printf ("getting %d training images from class %s\n", number_of_train_samples, class_labels[class_index].c_str());
		
		// Re-size the TrainSet vectors of sample matrixes
		Eigen::MatrixXd &train_raw_features = TrainSet->raw_features[class_index];

		train_raw_features.resize(signature_count, number_of_train_samples * tiles);

		TrainSet->class_samples[class_index].resize( tiles * number_of_train_samples );

		for( sample_index = 0; sample_index < number_of_train_samples; sample_index++ ) {
			train_raw_features.block(0,sample_index*tiles,signature_count,tiles) =
				raw_features[class_index].block(0,train_test_split[sample_index]*tiles,signature_count,tiles);
			for (int i=0; i < tiles; i++)
				TrainSet->class_samples[class_index].at((sample_index*tiles)+i) = class_samples[class_index].at( (train_test_split[sample_index]*tiles)+i );
		}

		// Record the number of training and testing samples in the split.
		split->training_images[ class_index ] = number_of_train_samples;

		if (number_of_test_samples) {
			split->testing_images[ class_index ] = number_of_test_samples;
			
			for( sample_index = number_of_train_samples; sample_index < (number_of_test_samples + number_of_train_samples); sample_index++ )
			{
				for (int i=0; i < tiles; i++)
					TestSet->test_samples.push_back (class_samples[class_index].at( (train_test_split[sample_index]*tiles)+i ));
			}
		}
		else
		{
			split->testing_images[ class_index ] = TestSet->class_nsamples [ class_index ] / tiles;
		}
	}

	return (1);
}

/* SplitAreas
   split into several classifiers based on the area (tile) of the image. E.g., a 4x4 tiling is divided into 16 sets. 
   tiles_num -long- the number of tiles (total number, not the square root of the number of tiles)
   TrainingSets -TrainingSet **- a pointer to an array of training sets.
*/
int TrainingSet::SplitAreas(long tiles_num, TrainingSet **TrainingSets)
{  int samp_index,tile_index;
   int res;
   for (tile_index=0;tile_index<tiles_num;tile_index++)
   {  TrainingSets[tile_index]=new TrainingSet((long)ceil((double)count/(double)tiles_num),class_num);   /* allocate memory for the new set of each tile location */
      SetAttrib(TrainingSets[tile_index]);
   }
   tile_index=0;
   for (samp_index=0;samp_index<count;samp_index++)
   {  if ( (res=TrainingSets[tile_index]->AddSample(samples[samp_index]->duplicate())) < 0) return (res);
      tile_index++;
      if (tile_index>=tiles_num) tile_index=0;
   }
   
   return (1);
}


/* AddAllSignatures
   load the image feature values for all samples from corresponding .sig files
   signatures know how to construct their .sig file names from their full_path (image path) and sample_name
*/

int TrainingSet::AddAllSignatures() {
	int samp_index;
	int sample_class;
	double sample_value;
	char buffer[IMAGE_PATH_LENGTH+SAMPLE_NAME_LENGTH+1];
	int res;

	for (samp_index=0;samp_index<count;samp_index++) {
	// Store the sample class and value in case its different in the file
		sample_class = samples[samp_index]->sample_class;
		sample_value = samples[samp_index]->sample_value;
		strcpy (buffer,samples[samp_index]->full_path);
		samples[samp_index]->Clear();
		// don't bother with locking except for the last sample.
		// FIXME: this doesn't really work.
		//    Easiest is some kind of global lock file for all processes, but that's unlikely.
		//    A sig file can exist and be empty and unlocked however briefly.

		errno = 0;
		res = 1;
		if (samples[samp_index]->count < 1) {
			res = samples[samp_index]->ReadFromFile(NULL,1);
		}

		if (res > 0) {
			samples[samp_index]->sample_class=sample_class; /* make sure the sample has the right class ID */
			samples[samp_index]->sample_value=sample_value; /* read the continouos value */
			strcpy (samples[samp_index]->full_path,buffer);

			// FIXME: Its not enough that there's more than one, the count has to match.
			// Really, the names have to match as well, but since we're dumping everything for now in fixed order, maybe OK.
			if (samples[samp_index]->count != signature_count && signature_count > 0) {
				catError ("Sample #%d, from '%s' has %d features, which does not match previous samples with %d features.\n",
					samp_index, samples[samp_index]->GetFileName(buffer), samples[samp_index]->count,signature_count);
				catError (" - Rename or delete the file to re-compute features.\n");
				return (INCONSISTENT_FEATURE_COUNT);
			} else if (signature_count == 0) {
				signature_count = samples[samp_index]->count;
			}
			
		} else { // report error
			catError ("Error reading feature values for sample %d from .sig file '%s'\n",samp_index,samples[samp_index]->GetFileName(buffer));
			return (CANT_LOAD_ALL_SIGS);
		}
	}
   return(count);
}

/* LoadFromPath
   load a dataset from the supplied path.  This is the primary method for loading datasets from disk.
   Returns < 0 on error.
   The rest of the parameters are passed through from LoadFromPath (described in AddImageFile, where they take effect)
   If the path is a directory:
     Scan files in directory.  If there are any image files, call LoadFromFilesDir on the given path
       In this case, the class assignments are unknown and this is effectively a test-set
     If there are no image files, but there are sub-directories,
       call LoadFromFilesDir on each sub-dir, using its name as the class label
   If the path is a file:
     If its a .fit file, read it using ReadFromFile
     If its an image file, make a single-sample test set by calling AddImageFile
       In this case, the class assignment is unknown and this is effectively a test-set.
     If its not a .fit file or an image file, its a file of filenames.
       Read each line of the file, in the format <file name><TAB><class>
       <class> can be a number, a class label or blank for unknowns (blank does not require a <TAB>).
       If all <class>es are purely numerical,
         Sample_value is assigned to the samples, sample_class is assigned 1, and class_num is set to 0 to do correlations.
       Otherwise,
         <class> is treated as a class label, the number of unique labels determines class_num.
         The sample_class is assigned the class index (in file order)
         Sample_value is assigned from converting the label into a double.
         Unknown classes go into class 0 with value 0 (sample_class = 0).
   If multi_processor is true, AddAllSignatures is called after all the class direcories are processed to load the skipped features.
*/
int TrainingSet::LoadFromPath(char *path, int save_sigs, featureset_t *featureset, int make_continuous, int skip_sig_comparison_check ) {
	int path_len = strlen(path);
	DIR *root_dir,*class_dir;
	struct dirent *ent;
	char buffer[512], filename[512], label[512], class_label[MAX_CLASS_NAME_LENGTH], *label_p;
	char classes_found[MAX_CLASS_NUM][MAX_CLASS_NAME_LENGTH];
	int res,n_classes_found=0, do_subdirs=0, class_found_index, class_index, file_class_num,pure_numeric=1;
	double samp_val;
	FILE *input_file=NULL;
	int fit_file=0;


	if (path[path_len-1]=='/') path[path_len-1]='\0';  /* remove a last '/' is there is one       */
	if ( (root_dir=opendir(path)) ) {
	// path is a directory
		while ( (ent = readdir(root_dir)) ) {
			if (!strcmp (ent->d_name,".") || !strcmp (ent->d_name,"..")) continue; // ignore . and .. sub-dirs
			sprintf(buffer,"%s/%s",path,ent->d_name);

		// A single supported image file makes this a directory of images (not classes)
			if (IsSupportedFormat(buffer)) {
			// The class assignment for these is unknown (we don't interpret directory elements in path)
			// So, these are loaded into the unknown class (class index 0).
				res=LoadFromFilesDir (path, 0, 0, save_sigs, featureset, skip_sig_comparison_check);
				errno = 0;
				if (res < 0) return (res);
			// Unknown classes are not pure numeric
				pure_numeric = 0;
			// Make sure we don't also process any sub-dirs we found previously
			// This also tells us we don't have any defined classes
				do_subdirs = 0;
				n_classes_found = 0;
				break; // Don't read any more entries.

			} else if ( (class_dir=opendir(buffer)) ) {
			// A directory with sub-directories
				snprintf(class_label,MAX_CLASS_NAME_LENGTH,"%s",ent->d_name);	/* the label of the class is the directory name */
			// Peek inside to make sure we have at least one recognized image file in there.
				do {
					ent = readdir(class_dir);
					if (ent) sprintf(filename,"%s/%s",buffer,ent->d_name);
				} while (ent && !IsSupportedFormat(filename));
				closedir(class_dir);
				errno = 0;
				if (ent) {
					do_subdirs = 1;
					if (n_classes_found < MAX_CLASS_NUM-1) {
						snprintf(classes_found[n_classes_found],MAX_CLASS_NAME_LENGTH,"%s",class_label);
						if ( !check_numeric (classes_found[n_classes_found],NULL) ) pure_numeric = 0; // across all class labels!
						n_classes_found++;
					} else {
						catError ("Classes in subdirectories of '%s' exceeds the maximum number of classes allowed (%d).\n",path,MAX_CLASS_NUM-1);
						return (TOO_MANY_CLASSES);
					}
				}
			}
		} // each dir entry.

	// We're done with analyzing the given path as a directory
		closedir(root_dir);
	// reset the system error from trying to open a file as a directory
		errno = 0;

	// Sort any classes we found
		qsort(classes_found,n_classes_found,sizeof(classes_found[0]),comp_strings);

	} else {
	// Its a file (image, .fit or file-of-files)
	// reset the system error
		errno = 0;

		if (IsSupportedFormat(path)) {

		// A single supported image file
			res = AddImageFile(path, 0, 0, save_sigs, featureset, skip_sig_comparison_check);
			if (res < 1) return (res-1);
		// For a set of unknowns, number of classes is 1, with all samples sample_class = 0
			class_num = 1;
		// Unknown classes are not pure numeric
			pure_numeric = 0;

		} else if (IsFitFile (path)) {
		// Its a .fit file

			if (ReadFromFile (path) < 1) return (CANT_OPEN_FIT);
			fit_file=1;

		} else if ( (input_file=fopen(path,"r")) ) {
		// read the images from a file of filenames

			n_classes_found = 0;
			// reset the system error
			errno = 0;
			while (fgets(buffer,sizeof(buffer),input_file)) {
				if (*buffer == '#') continue; // skip comment lines

			// Read chars while not \n,\r,\t, read and ignore chars that are \n\r\t, read chars while not \n,\r,\t.
			// Basically, the first two tab-delimited strings on a line.
				*filename = *label = *class_label = '\0';
				res = sscanf (buffer," %[^\n\r\t]%*[\t\r\n]%[^\t\r\n]",filename,label);
				if (res < 1) continue;
				if (!IsSupportedFormat(filename)) {
					catError ("File '%s' doesn't look like a supported image file format - skipped\n.",filename);
					continue; // skip unrecognized files
				}

				if (*label) {
				// determine class index and value from label
					snprintf (class_label,MAX_CLASS_NAME_LENGTH,"%s",label); // conform to size restriction

				// Search for the label in pre-existing labels
					label_p = (char *)bsearch(class_label, classes_found, n_classes_found, sizeof(classes_found[0]), comp_strings);

					if (label_p) {
					// Class was previously found in the file - convert bsearch pointer to index
						file_class_num = (int) ( (label_p - classes_found[0]) / sizeof(classes_found[0]) ) + 1;

					} else {
					// New label - NO pre-existing class found
						if (n_classes_found < MAX_CLASS_NUM-1) {
							snprintf(classes_found[n_classes_found],MAX_CLASS_NAME_LENGTH,"%s",class_label);	/* the label of the class is the directory name */
							if ( !check_numeric (classes_found[n_classes_found],NULL) ) pure_numeric = 0; // across all class labels!
							n_classes_found++;
						} else {
							catError ("Classes in file '%s' exceeds the maximum number of classes allowed (%d).\n",path,MAX_CLASS_NUM-1);
							return (TOO_MANY_CLASSES);
						}
					// Sort any classes we found
					// Note that we have to re-sort the list for every class we find in order for bsearch above to work.
						qsort(classes_found,n_classes_found,sizeof(classes_found[0]),comp_strings);
					} // new label
				} // got a label from file
			} // while reading file of filenames
		// Rewind the file to load the samples if we have one.
			if (n_classes_found) rewind (input_file);
		} // opened file of filenames
	} // processing a non-directory path

	if (!fit_file) {
	// We're done processing the path.
	// If we found classes to process, process them
	
	// If pure_numeric is true, make_continuous can be 1.
	// Otherwise, its a warning for make_continuous to be 1 while pure_numeric is false.
		if (!pure_numeric && make_continuous) {
			catError ("WARNING: Trying to make a continuous dataset with non-numeric class labels.  Making discrete classes instead.\n");
		} else if (make_continuous && n_classes_found < 1) {
			catError ("WARNING: Trying to make a continuous dataset with no defined classes found.  Samples are unknown.\n");
		} else if (make_continuous) {
			res = AddContinuousClass (NULL);
			if (res < 0) return (res);
		}
	
	
	// Process the classes we found, and the subdirectories if we found them
		for (class_found_index=0;class_found_index<n_classes_found;class_found_index++) {
		// Create class.
			if (is_continuous) class_index = CONTINUOUS_CLASS_INDEX;
			else {
				class_index = AddClass (classes_found[class_found_index]);
				// This may barf for various reasons.
				if (class_index < 0) return (class_index);
			}
	
			check_numeric (classes_found[class_found_index],&samp_val);
	
		// LoadFromFilesDir sorts the samples (files)
			if (do_subdirs) {
				sprintf(buffer,"%s/%s",path,classes_found[class_found_index]);
			// reset the system error
				errno = 0;
				res=LoadFromFilesDir (buffer, class_index, samp_val, save_sigs, featureset, skip_sig_comparison_check);
				if (res < 0) return (res);
			// Since we made the class, we have to get rid of it if its empty.
				if (class_nsamples[class_index] < 1) {
					RemoveClass (class_index);
				}
			}
		}
	
		if (input_file) {
		// The above has created all the classes, so here, we just add samples
			rewind (input_file);
			// A lot of this is copy-paste code from above to accomplish two reads of this file.
			// The first pass gave us an ordered list of classes.  In the second pass, we add samples.
			// We need to have a sorted list of classes, and add them in order, yet we want to keep the samples in file order.
			// The alternative is to read the file once, and accomplish two passes by holding all of its relevant contents in memory.
			// Or worse, sort the classes and reindex the samples as we go.
			// reset the system error
				errno = 0;
			while (fgets(buffer,sizeof(buffer),input_file)) {
				if (*buffer == '#') continue; // skip comment lines
	
			// Read chars while not \n,\r,\t, read and ignore chars that are \n\r\t, read chars while not \n,\r,\t.
			// Basically, the first two tab-delimited strings on a line.
				*filename = *label = *class_label = '\0';
				res = sscanf (buffer," %[^\n\r\t]%*[\t\r\n]%[^\t\r\n]",filename,label);
				if (res < 1) continue;
				if (!IsSupportedFormat(filename)) continue; // skip unrecognized files
	
				if (*label) {
				// determine class index and value from label
					snprintf (class_label,MAX_CLASS_NAME_LENGTH,"%s",label); // conform to size restriction
	
				// Search for the label in pre-existing labels
					if (!is_continuous) {
						label_p = (char *)bsearch(class_label, classes_found, n_classes_found, sizeof(classes_found[0]), comp_strings);
						if (label_p) file_class_num = (int) ( (label_p - classes_found[0]) / sizeof(classes_found[0]) ) + 1;
						else file_class_num = UNKNOWN_CLASS_INDEX; // This should never happen.
					} else {
						file_class_num = CONTINUOUS_CLASS_INDEX;
					}
					check_numeric (class_label,&samp_val);
				} else {
					file_class_num = UNKNOWN_CLASS_INDEX;
					samp_val = 0;
				}
	
			// reset the system error
				errno = 0;
				res = AddImageFile(filename, file_class_num, samp_val, save_sigs, featureset, skip_sig_comparison_check);
				if (res < 0) return (res);
	
			} // while reading file of filenames
			fclose (input_file);
	
		// Finally, we need to make sure all the classes we created have some samples
			for (class_index=1;class_index<class_num;class_index++) {
				if (class_nsamples[class_index] < 1) RemoveClass (class_index);
			}
		}
	
	
	// Done processing path as a dataset.
	// Load all the sigs if other processes are calculating them
		if ( (res  = AddAllSignatures ()) < 0) return (res);
	} else { // its a fit file!
		if (!is_numeric && make_continuous) {
			catError ("WARNING: Trying to make a continuous dataset with non-numeric class labels.  Making discrete classes instead.\n");
		} else if (make_continuous && class_num < 1) {
			catError ("WARNING: Trying to make a continuous dataset with no defined classes found.  Samples are unknown.\n");
		} else if (make_continuous) {
			MakeContinuous (NULL);
		}
	}

// Check what we got.
	if (count < 1) {
		catError ("No samples read from '%s'\n", path);
		return (count);
	}
	if (signature_count != featureset->n_features) {
		catError ("WARNING: Number of features specified (%d) do not match the number collected from '%s' (%d)\n", featureset->n_features, path, signature_count);
		catError ("         Either command-line options don't match those stored in the dataset (.fit) file, or the file has been corrupted\n");
	}

// Set the path and name
	strcpy (source_path,path);
	char *char_p = source_path+strlen(source_path)-1;
	// kill terminal '/', ' ', '\t', etc
	while ( char_p > source_path && *char_p && (*char_p == ' ' || *char_p == '\t' || *char_p == '\r' || *char_p == '\n' || *char_p == '/') )
		*char_p-- = '\0';
	char_p = strrchr(source_path,'/');
	if (char_p) char_p++;
	else char_p = source_path;
	strcpy(name,char_p);
	if (strrchr(name,'.')) *strrchr(name,'.')='\0';

	Summarize(featureset);
	return (1);
}




/* LoadFromFilesDir
   load images from the specified path, assigning them to the specified class, giving them the specified value
     Both can be 0 if the class is unknown
   The rest of the parameters are passed through from LoadFromPath (described in AddImageFile, where they take effect)
   Scan the files in the directory, calling AddImageFile on each image file encountered.
   If multi_processor is true, AddAllSignatures should be called after all the class direcories are processed to load the skipped features.
*/
int TrainingSet::LoadFromFilesDir(char *path, unsigned short sample_class, double sample_value, int save_sigs, featureset_t *featureset, int skip_sig_comparison_check ) {
	DIR *class_dir;
	struct dirent *ent;
	char img_basenames[MAX_FILES_IN_CLASS][64];
	char sig_basenames[MAX_FILES_IN_CLASS][64];
	int res=1,files_in_class_count=0,n_sig_basenames=0,n_img_basenames=0,file_index;
	char buffer[512],*char_p,*sig_fullpath = NULL;
	FILE *sigfile;

	if( verbosity >=2 ) printf ("Processing directory '%s'\n",path);
	if (! (class_dir=opendir(path)) ) { catError ("Can't open directory %s\n",path); return (0); }
	while ( (ent = readdir(class_dir)) ) {
		if (!strcmp (ent->d_name,".") || !strcmp (ent->d_name,"..")) continue;
		if (!IsSupportedFormat(ent->d_name)) continue;

		// In order to ensure we gather all of the specified samples for .sig files,
		// we need to determine the image name that the sigfiles refer to, and pass this to AddImageFile
		// We also need to consolidate the img_basename list and the sig_basename list to remove duplicates
		// Should really be using an std::map for this
		if ( (char_p = strstr(ent->d_name,".sig")) && *(char_p+4) == '\0') {
			sprintf (buffer,"%s/%s",path,ent->d_name);
			if ( (sigfile = fopen (buffer,"r")) ) {
				// first line is classname, second line is full_path
				*buffer = '\0';
				if ( fgets (buffer , 512 , sigfile) ) sig_fullpath = fgets (buffer , 512 , sigfile);
				fclose (sigfile);
				if (sig_fullpath && *sig_fullpath) { // not empty
				 // the leading paths may not be correct for all sigs (i.e. NFS mounts with different mountpoints)
				 // The only thing we care about right now is the set of sig files in this directory stemming from the same base image.
					char_p = strrchr (sig_fullpath,'/');
					if (!char_p) char_p = sig_fullpath; // in case its just the file
					else char_p++;
					chomp (char_p);
					if (!bsearch (char_p, sig_basenames, n_sig_basenames, sizeof(sig_basenames[0]), comp_strings)) {
						strcpy(sig_basenames[n_sig_basenames++],char_p);
						qsort(sig_basenames,n_sig_basenames,sizeof(sig_basenames[0]), comp_strings);
					}
				} // empty means somebody else is taking care of it.
			// if it exists and we can't open it, then there's an error.
			} else {
				catError ("Sig file '%s/%s' could not be opened.\n",path,ent->d_name);
				return (0);
			}
		// its an image file
		} else {
			strcpy(img_basenames[n_img_basenames++],ent->d_name);
		}
	}
	closedir(class_dir);
	qsort(img_basenames,n_img_basenames,sizeof(img_basenames[0]), comp_strings);
	// remove files from sig_basenames that exist in img_basenames
	for (file_index = 0; file_index < n_sig_basenames; file_index++) {
		if (bsearch (sig_basenames[file_index], img_basenames, n_img_basenames, sizeof(img_basenames[0]), comp_strings)) {
			n_sig_basenames--;
			if (file_index < n_sig_basenames)
				memmove( &(sig_basenames[file_index]),&(sig_basenames[file_index+1]),sizeof(img_basenames[0])*(n_sig_basenames-file_index) );
			file_index--;
		}
	}
	// copy what's left in sig_basenames to img_basenames, and sort it again.
	for (file_index = 0; file_index < n_sig_basenames; file_index++)
		strcpy(img_basenames[n_img_basenames++],sig_basenames[file_index]);
	qsort(img_basenames,n_img_basenames,sizeof(img_basenames[0]), comp_strings);

//for (file_index = 0; file_index < n_img_basenames; file_index++)
//printf ("final basename: '%s'\n",img_basenames[file_index]);
	
	// N.B.: A call to AddClass must already have occurred, otherwise AddSample called from AddImageFile will fail.

	// Process the files in sort order
	for (file_index=0; file_index<n_img_basenames; file_index++) {
		sprintf(buffer,"%s/%s",path,img_basenames[file_index]);
		res = AddImageFile(buffer, sample_class, sample_value, save_sigs, featureset, skip_sig_comparison_check);
		if (res < 0) return (res);
		else files_in_class_count += res; // May be zero
	}
	return (files_in_class_count);
}

/* AddImageFile
   load a set of features to the dataset from one image_path on disk by calculating features if necessary/possible.
   This includes any tiling to be done on the image, down-sampling, etc
   Loading of image files is done lazily, so that if the set of .sigs expected for the image is on disk, the image file will never be opened or read.
   multi_processor -int- flag to allow skipping feature calculation based on the presence of a corresponding .sig file.
     If multi_processor is set:
        If a .sig exists, it will be assumed that these features are being calculated elsewhere (or where already calculated)
          The signatures (features) will be invalid, but the full_path will be set, and it will be added to the dataset.
          The method AddAllSignatures should be called after file processing to load the skipped signatures.
        If a .sig does not exist, features will be computed, and the .sig file saved.
     If multi_processor is not set, features will be computed and not saved.
     The ImageSignatures will be added to the dataset with or without valid features due to skipping in multi-processor mode.
       This is done so that the sample order is set in one place only by the order of calling this method.
   sample_class -unsigned short- class index to assign this image to (may be 0 for 'unknown')
   sample_value -double- a continuous value for the image
   The rest of the parameters are passed through from LoadFromPath
     tiles -int- the number of tiles to break the image to (e.g., 4 means 4x4 = 16 tiles)
     multi_processor -int- 1 if more than one signatures process should be running
     large_set -int- whether to use the large set of image features or not
     compute_colors -int- wether or not to compute color features.
     downsample -int- 1-100 percent down-sampling.
     mean -double- normalize to intensity mean.
     stddev -double- normalize to stddev as well as mean.
     bounding_rect -rect *- a sub image area from which features are computed. ignored if NULL.
     overwrite -int- 1 for forcely overwriting pre-computed .sig files
   skip_sig_comparison_check -int- true if the user wants to bypass checking if the current experiment params match those in the pre-computed sig.
   Returns 0 if the image cannot be opened, 1 otherwise.
   If multi_processor is true, AddAllSignatures should be called after all the class files are processed to load the skipped features.
*/

 
int TrainingSet::AddImageFile(char *filename, unsigned short sample_class, double sample_value, int save_sigs, featureset_t *featureset, int skip_sig_comparison_check ) {
	int res=0;
	int sample_index;
	signatures *ImageSignatures;
	char buffer[IMAGE_PATH_LENGTH];
	FILE *sigfile;
	int sig_index,n_sigs=0;

	struct siginfo_s {
		signatures *sig;
		FILE *file;
		int rot_index;
		int tile_index_x;
		int tile_index_y;
		bool saved;
		bool added;
	} our_sigs[MAX_SAMPLES_PER_IMAGE];


	if (verbosity>=2) printf ("Processing image file '%s'.\n",filename);

// pre-determine sig files for this image.
// Primarily, this lets us pre-lock all the signature files for one image (see below).
// The side-effect is that we separate the code that sets up sampling parameters from the
// code that does the actual sampling and feature calculation.
// Eventually, the sampling code would live here, and be generic but the set-up of the parameters would be done elsewhere
// It seems this would be a good application of functional programming (i.e. closures, functors, etc).

	our_sigs[0].sig = NULL;
	our_sigs[0].file = NULL;
	our_sigs[0].rot_index = our_sigs[0].tile_index_x = our_sigs[0].tile_index_y = -1;
	for (sample_index=0; sample_index < featureset->n_samples; sample_index++) {
	// make signature objects for samples
		ImageSignatures=new signatures;
		ImageSignatures->NamesTrainingSet=this;
		strcpy(ImageSignatures->full_path,filename);
		ImageSignatures->sample_class=sample_class;
		ImageSignatures->sample_value=sample_value;

	// set the sample name and try to read it from disk.
	// This will acquire a lock if the sample doesn't exist
	// Note that we're acquiring locks for all the sig files for this image because its inefficient
	// for multiple processes to read the same image and compute different sub-sets of the same sig-set.
	// Initially, multiple processes will "win" on one image and do this anyway, but eventually they will become de-synchronized.
	// The image file itself could be locked to prevent this, but this would be more complicated:
	//  * are the other processes really computing the same sate of sigs?  Not necessarily.
	//  * we would have to wait for the image lock to clear and issue locks on any left over sig files that weren't locked while we waited.
		strcpy (ImageSignatures->sample_name,featureset->samples[sample_index].sample_name);
	// ask for an exclusive write-lock if file doesn't exist
	// if its the last sample, then we wait for the lock.
		res = ImageSignatures->ReadFromFile(&sigfile,0);
		if (res == 0 && sigfile) { // got a lock: file didn't exist previously, and is not locked by another process.
			if (verbosity>=2) printf ("Adding '%s' for sig calc.\n",ImageSignatures->GetFileName(buffer));
			our_sigs[n_sigs].sig = ImageSignatures;
			our_sigs[n_sigs].file = sigfile;
			our_sigs[n_sigs].rot_index = featureset->samples[sample_index].rot_index;
			our_sigs[n_sigs].tile_index_x = featureset->samples[sample_index].tile_index_x;
			our_sigs[n_sigs].tile_index_y = featureset->samples[sample_index].tile_index_y;
		// Initialize the next one 
			n_sigs++;
			our_sigs[n_sigs].sig = NULL;
			our_sigs[n_sigs].file = NULL;
			our_sigs[n_sigs].rot_index = our_sigs[n_sigs].tile_index_x = our_sigs[n_sigs].tile_index_y = -1;

		} else if (res == 0) {
		// File already has a lock.
			if (verbosity>=2) printf ("Sig '%s' being processed by someone else\n",ImageSignatures->GetFileName(buffer));
			if ( (res=AddSample(ImageSignatures)) < 0) break;

		} else if (res == NO_SIGS_IN_FILE) {
		// File exists and lockable, but no sigs
			catError ("File '%s' has no data. Processing may have prematurely terminated, or file locking may not be functional.\n"
				"Delete the file and try again.\n",ImageSignatures->GetFileName(buffer));
			break;

		} else if (res < 0) {
		// no lock or sig file, couldn't create, other errors
			catError ("Error locking/creating '%s'.\n",ImageSignatures->GetFileName(buffer));
			break;

		} else if (res > 0) {
		// file was successfully read in (no write lock, file present, samples present).
		// over-write these fields read in from the file
			strcpy(ImageSignatures->full_path,filename);
			ImageSignatures->sample_class=sample_class;
			ImageSignatures->sample_value=sample_value;
			if (verbosity>=2) printf ("Sig '%s' read in.\n",ImageSignatures->GetFileName(buffer));
			if ( (res=AddSample(ImageSignatures)) < 0) break;
		}
	}
	
	if (res < 0) {
		for (sample_index=0; sample_index < n_sigs; sample_index++) {
			if (our_sigs[sample_index].file) {
				our_sigs[sample_index].sig->FileClose (our_sigs[sample_index].file);
				unlink (our_sigs[sample_index].sig->GetFileName(buffer));
			}
			if (our_sigs[sample_index].sig) delete our_sigs[sample_index].sig;
		}
		return (res);
	}

	// FIXME: the last sample may be being processed by someone else, and if so, we will fail on AddAllSignatures
	// wait on a lock or successful read?

	// lazy loading of images and samples.
	// this could be better done using something more implicit and general, maybe a closure (a functor? its functadelic!)
	// Lazy loading could have been done while generating the sampling parameters above, but this lets us pre-obtain file locks
	// for all the sigs we will calculate.  The code separation b/w sampling parameter setup and the sampling itself points to
	// doing this in a more general way with functional programming (or some other technique).
	ImageMatrix *image_matrix=NULL, *rot_matrix=NULL, *tile_matrix=NULL;
	int rot_matrix_indx=0;
	int tiles_x = featureset->sampling_opts.tiles_x, tiles_y = featureset->sampling_opts.tiles_y, tiles = tiles_x * tiles_y;
	preproc_opts_t *preproc_opts = &(featureset->preproc_opts);
	feature_opts_t *feature_opts = &(featureset->feature_opts);
	int rot_index,tile_index_x,tile_index_y;
	for (sig_index = 0; sig_index < n_sigs; sig_index++) {
		ImageSignatures = our_sigs[sig_index].sig;
		sigfile = our_sigs[sig_index].file;
		rot_index = our_sigs[sig_index].rot_index;
		tile_index_x = our_sigs[sig_index].tile_index_x;
		tile_index_y = our_sigs[sig_index].tile_index_y;
		our_sigs[sig_index].saved = false; // don't unlink if true
		our_sigs[sig_index].added = false; // don't delete if true
		if (verbosity>=2) printf ("processing '%s' (index %d).\n",ImageSignatures->GetFileName(buffer),sig_index);

		if (!image_matrix) { // for all samples
			image_matrix = new ImageMatrix;
		// any pre-existing sig files may have different paths for the image (i.e. different NFS mountpoints, etc.)
		// One of these could be reachable if the image is not in the same directory as the sigs.
		// There is no support for this now though - its an error for the image not to exist together with the sigs
		// if we need to open the image to recalculate sigs (which we only need if one or more sigs is missing).
//	if (verbosity>=2) printf("Loading image %s\n",filename);
			if ( (res = image_matrix->OpenImage(filename,preproc_opts->downsample,&(preproc_opts->bounding_rect),(double)preproc_opts->mean,(double)preproc_opts->stddev)) < 1) {
				catError ("Could not read image file '%s' to recalculate sigs.\n",filename);
				res = -1; // make sure its negative for cleanup below
				break;
			}
			if (rot_index == 0) {
				rot_matrix_indx = 0;
				rot_matrix = image_matrix;
			}
		}
		// Since image opening was lazy, everything else is too.
		if (rot_matrix_indx != rot_index) {
			if (rot_matrix_indx != 0 && rot_matrix) delete rot_matrix;
			rot_matrix = NULL;
		}
		if (!rot_matrix) {
			if (rot_index > 0) { //rotate the image_matrix
				rot_matrix = image_matrix->Rotate (90.0 * rot_index);
			} else {
				rot_matrix = image_matrix;
			}
			rot_matrix_indx = rot_index;
			if (tiles == 1) tile_matrix = rot_matrix;
		}
		if (!tile_matrix) {
			long tile_x_size;
			long tile_y_size;
			if (rot_index == 1 || rot_index == 3) {
				tile_y_size=(long)(rot_matrix->width/tiles_x);
				tile_x_size=(long)(rot_matrix->height/tiles_y);
			} else {
				tile_x_size=(long)(rot_matrix->width/tiles_x);
				tile_y_size=(long)(rot_matrix->height/tiles_y);
			}
			tile_matrix = new ImageMatrix(rot_matrix,
				tile_index_x*tile_x_size,tile_index_y*tile_y_size,
				(tile_index_x+1)*tile_x_size-1,(tile_index_y+1)*tile_y_size-1,0,0);
		}

	// last ditch effort to avoid re-computing all sigs: see if an old-style sig file exists, and has
	// a set of sigs that matches a small subset of re-computed sigs.
		char old_sig_filename[IMAGE_PATH_LENGTH+SAMPLE_NAME_LENGTH+1], *char_p;
		strcpy (old_sig_filename,ImageSignatures->full_path);
		if ( (char_p = strrchr (old_sig_filename,'.')) ) *char_p = '\0';
		else char_p = old_sig_filename+strlen(old_sig_filename);
		sprintf (char_p,"_%d_%d.sig",tile_index_x,tile_index_y);
		if( skip_sig_comparison_check || (res=ImageSignatures->CompareToFile(tile_matrix,old_sig_filename,feature_opts->compute_colors,feature_opts->large_set)) ) {
			ImageSignatures->LoadFromFile (old_sig_filename);
			if (ImageSignatures->count < 1) {
				catError ("Error converting old sig file '%s' to '%s'. No samples in file.\n",old_sig_filename,ImageSignatures->GetFileName(buffer));
				res=0;
			} else {
				catError ("Old signature file '%s' converted to '%s' with %d features.\n",old_sig_filename,ImageSignatures->GetFileName(buffer),ImageSignatures->count);
				strcpy(ImageSignatures->full_path,filename);
				ImageSignatures->sample_class=sample_class;
				ImageSignatures->sample_value=sample_value;

				unlink (old_sig_filename);
			}
		}

	// all hope is lost - compute sigs.
		if (!res) {
			// CEC_const vector<const FeatureGroup*> work_order;
			if( work_order.size() <= 0 )
				ImageSignatures->GenerateStandardFeatureGroupList( feature_opts->large_set, feature_opts->compute_colors, work_order );
			ImageSignatures->ComputeFromGroupList( tile_matrix, work_order );
		}
	// we're saving sigs always now...
	// But we're not releasing the lock yet - we'll release all the locks for the whole image later.
	// This uses our open sigfile handle, so it doesn't call close on it, which would release the lock.
		ImageSignatures->SaveToFile (sigfile,1);
		our_sigs[sig_index].saved = true;
		if ( (res=AddSample(ImageSignatures)) < 0) {
			break;
		}
		our_sigs[sig_index].added = true;

		if (tiles > 1) {
			delete tile_matrix;
			tile_matrix = NULL;
		}
	}
	
// don't release any locks until we're done with this image
// this prevents another process from opening the same image to calculate a different sub-set of sigs
	for (sig_index = 0; sig_index < n_sigs; sig_index++) {
		if (our_sigs[sig_index].file) {
			our_sigs[sig_index].sig->FileClose (our_sigs[sig_index].file);
		}
		if (!our_sigs[sig_index].saved) {
			unlink (our_sigs[sig_index].sig->GetFileName(buffer));
		}
		if (!our_sigs[sig_index].added) {
			delete (our_sigs[sig_index].sig);
		}
	}

	if (rot_matrix && rot_matrix != image_matrix) delete rot_matrix;
	if (image_matrix) delete image_matrix;

	return (res);
}


/* Classify 
   Classify a test sample.
   TestSet -TrainingSet *- one or more tiles of one or more test image
   test_sample_index -int- the index of the image in TestSet that should be tested. if tiles, then the first tile.
   max_tile -int- just use the most similar tile instead of averaging all times
   returned value can be either a class index, or if is_continuous a contiouos value
*/

double TrainingSet::ClassifyImage(TrainingSet *TestSet, int test_sample_index,int method, int tiles, int tile_areas, TrainingSet *TilesTrainingSets[], int max_tile, int rank, data_split *split, double *similarities)
{  int predicted_class=0,tile_index,class_index,cand,sample_class,test_tile_index,interpolate=1;
   double probabilities[MAX_CLASS_NUM],probabilities_sum[MAX_CLASS_NUM],normalization_factor,normalization_factor_avg=0;
   signatures *closest_sample=NULL, *tile_closest_sample=NULL,*test_signature;
   char interpolated_value[128],last_path[IMAGE_PATH_LENGTH];
   TrainingSet *ts_selector;
   int most_similar_tile=1,most_similar_predicted_class=0;
   double val=0.0,sum_prob=0.0,dist,value=0.0,most_similar_value=0.0,closest_value_dist=INF,max_tile_similarity=0.0;  /* use for the continouos value */
   int do_html=0;
   char buffer[512],closest_image[512],color[128],one_image_string[MAX_CLASS_NUM*15];

// If its a pure testset, use the samples vector instead of test_samples
	std::vector<signatures *> &testset_samples = TestSet->test_samples.size() > 0 ? TestSet->test_samples : TestSet->samples;

   /* interpolate only if all class labels are values */
	interpolate=is_numeric;
	if (tiles<=0) tiles=1;   /* make sure the number of tiles is valid */
	strcpy(last_path,testset_samples[test_sample_index]->full_path);

 	if (TestSet->train_class) {
 		sample_class = TestSet->train_class [ TestSet->samples[test_sample_index]->sample_class ];
 	} else {
 		sample_class = TestSet->samples[test_sample_index]->sample_class;   /* the ground truth class of the test sample */
 	}

	for( class_index = 1; class_index <= class_num; class_index++ )
		probabilities_sum[ class_index ] = 0.0;  // initialize the array
	
	for (tile_index=test_sample_index;tile_index<test_sample_index+tiles;tile_index++) {
		if (verbosity>=2 && tiles>1)
			printf("%s (%d/%d)\t",testset_samples[tile_index]->full_path,1+tile_index-test_sample_index,tiles);
		test_signature = testset_samples[ tile_index ];
		if (tile_areas==0 || tiles==1)
			ts_selector=this;
		else 
			ts_selector = TilesTrainingSets[ tile_index - test_sample_index ];   /* select the TrainingSet of the location of the tile */
		if( is_continuous ) { //interpolate the value here 
			val = ts_selector->InterpolateValue( test_signature, method, rank, &closest_sample, &dist );
			value = value + val / ( double ) tiles;
			if( verbosity>=2 && tiles > 1 ) {
				if( sample_class )
					printf( "%.3g\t%.3g\n", testset_samples[ test_sample_index ]->sample_value, val );
				else
					printf( "N/A\t%.3g\n", val );
			}
		} else {
			if( method == WNN )
				predicted_class = ts_selector->WNNclassify( test_signature, probabilities, &normalization_factor, &closest_sample );
			if( method == WND )
				predicted_class = ts_selector->classify2( TestSet->samples[ test_sample_index ]->full_path, test_sample_index, test_signature, probabilities, &normalization_factor );
			// This should not really happen...
			if (predicted_class < 1) {
				predicted_class = 0;
			}

			if( verbosity>=2 && tiles>1) {
				printf( "%.3g\t", normalization_factor );
				for( class_index = 1; class_index <= class_num; class_index++)
					printf( "%.3f\t", probabilities[ class_index ] );
				if( sample_class )
					std::cout << class_labels[ sample_class ] << "\t" <<  class_labels[ predicted_class ];
				else
					std::cout << TestSet->class_labels[TestSet->samples[ test_sample_index ]->sample_class]
					          << "\t" << class_labels[ predicted_class ];

				if (interpolate) {
					testset_samples[ test_sample_index ]->interpolated_value = 0;
					for( class_index = 1; class_index <= class_num; class_index++ )
						testset_samples[ test_sample_index ]->interpolated_value += 
							probabilities[ class_index ] * atof (TestSet->class_labels [class_index].c_str()) ;
					printf ("\t%.3f",testset_samples[ test_sample_index ]->interpolated_value);
				}
				printf( "\n" );
			}
	//if (method==WND) predicted_class=this->classify3(test_signature, probabilities, &normalization_factor);
		}
	
		  /* use only the most similar tile */
		if (max_tile) {
			sum_prob=0.0;
			for (class_index=0;class_index<=class_num;class_index++) if (class_index!=predicted_class) sum_prob+=probabilities[class_index];
			if (is_continuous) {
				if (dist<closest_value_dist) {
					closest_value_dist=dist;
					most_similar_value=val;
					most_similar_tile=tile_index;		   
					tile_closest_sample=closest_sample;
				}
			} else if (probabilities[predicted_class]/sum_prob>max_tile_similarity) {
				max_tile_similarity=probabilities[predicted_class]/sum_prob;
				most_similar_tile=tile_index;
				most_similar_predicted_class=predicted_class;
				tile_closest_sample=closest_sample;			
			}
		}
		  
		  /* measure the distances between the image to all other images */
		if (split && split->image_similarities) {
			Eigen::VectorXd sample_vec;
			split->image_similarities[(1+test_sample_index/tiles)]=(double)(test_signature->sample_class);   /* for storing the class of each image in the first row (that is not used for anything else) */
			for (test_tile_index=0;test_tile_index<TestSet->count;test_tile_index++) {
				signatures *compare_to;
				if (max_tile) compare_to=testset_samples[most_similar_tile]->duplicate();         /* so that only the most similar tile is used */
				else compare_to=testset_samples[test_tile_index]->duplicate();          
				compare_to->normalize(this, sample_vec);   /* in order to compare two normalized vectors */
				split->image_similarities[(1+test_sample_index/tiles)*(TestSet->count/tiles+1)+test_tile_index/tiles+1]+=(distance(test_signature,compare_to,2.0)/tiles);
				delete compare_to;
			}
		}
		  
		if ((strcmp(last_path,test_signature->full_path)!=0)) printf("inconsistent tile %d of image '%s' \n",tile_index-test_sample_index,test_signature->full_path); /* check that the tile is consistent */
		for (class_index=1;class_index<=class_num;class_index++) 
			if (max_tile && max_tile_similarity==probabilities[predicted_class]/sum_prob) probabilities_sum[class_index]=probabilities[class_index];  /* take the probabilities of this tile only */
			else probabilities_sum[class_index]+=(probabilities[class_index]/(double)tiles);  /* sum the marginal probabilities */	  
		normalization_factor_avg+=normalization_factor;	  
		if (split && split->tile_area_accuracy) split->tile_area_accuracy[tile_index-test_sample_index]+=((double)(predicted_class==sample_class))/((double)TestSet->count/(double)tiles); 
	} /* iterate over tiles */

   if (max_tile) 
   {  value=most_similar_value;
      predicted_class=most_similar_predicted_class;
   }
   
   if (tiles>1)
     closest_sample=tile_closest_sample;
   
   if (is_continuous) testset_samples[test_sample_index]->interpolated_value=value;       
   normalization_factor_avg/=tiles;

   /* find the predicted class based on the rank */
   for (class_index=1;class_index<=class_num;class_index++) probabilities[class_index]=0.0;  /* initialize the array */
   if (class_num>1) // continuous and discrete
     for (cand=0;cand<rank;cand++)
     {  double max=0.0;
        for (class_index=1;class_index<=class_num;class_index++)
          if (probabilities_sum[class_index]>max && probabilities[class_index]==0.0)
          {  max=probabilities_sum[class_index];
             predicted_class=class_index;
          }	
          probabilities[predicted_class]=1.0;
          if (predicted_class==sample_class) break;  /* class was found among the n closest */
     }

//if (probabilities[1]>0.995) predicted_class=(1);
//else predicted_class=(2);

   /* update confusion and similarity matrices */
   if (split && split->confusion_matrix)  /* update the confusion matrix */
	 split->confusion_matrix[class_num*sample_class+predicted_class]++;
   if (split && split->similarity_matrix && class_num>0) /* update the similarity matrix */
	 for (class_index=1;class_index<=class_num;class_index++) split->similarity_matrix[class_num*sample_class+class_index]+=probabilities_sum[class_index];

   /* print the report line to a string (for the final report) */
	if (split && split->individual_images) do_html = 1;

	if (do_html) sprintf(one_image_string,"<tr><td>%d</td>",(test_sample_index/tiles)+1);  /* image index */
	if (verbosity>=1) {
		printf("%s",testset_samples[test_sample_index]->full_path);
		if (tiles > 1) printf(" (AVG)");
		printf ("\t");
	}

	if (!is_continuous && (do_html || verbosity>=1)) { /* normlization factor */
		if (do_html) {
			sprintf( buffer,"<td>%.3g</td>", normalization_factor_avg );
			strcat(one_image_string, buffer);
		}
		if (verbosity>=1) printf ("%.3g\t",normalization_factor_avg);
	}
	if (do_html || verbosity>=1) {
		for (class_index=1;class_index<=class_num;class_index++) {
			if (do_html) {
				if (class_index==sample_class) sprintf(buffer,"<td><b>%.3f</b></td>",probabilities_sum[class_index]);  /* put the actual class in bold */
			 	else sprintf(buffer,"<td>%.3f</td>",probabilities_sum[class_index]);
			 	strcat(one_image_string,buffer);
			 }
			if (verbosity>=1) printf ("%.3f\t",probabilities_sum[class_index]);
		}
		if (do_html) {
			if (sample_class) {
				if (predicted_class==sample_class) sprintf(color,"<font color=\"#00FF00\">Correct</font>");
				else sprintf(color,"<font color=\"#FF0000\">Incorrect</font>");
			} else {
				sprintf(color,"<font color=\"#00FFFF\">Predicted</font>");
			}
		}
	}

      /* add the interpolated value */
	if (interpolate) {
		if( !is_continuous && class_num > 1 )  {/* interpolate by the values of the class names is is_continuous, we already did it by continuous classification */
          // Method 1: create an interpolated value based only on the top
          // two marginal probabilities
//          double second_highest_prob = -1.0, min_prob = INF;
//          int second_highest_class;
//          for( class_index = 1; class_index <= class_num; class_index++ )
//          if( probabilities_sum[class_index] < min_prob )
//            min_prob=probabilities_sum[class_index];
//
//          /* subtract the min value from all classes to reduce the noise */
//          for( class_index = 1; class_index <= class_num; class_index++ )
//            probabilities_sum[class_index] -= min_prob;
//
//          for (class_index=1;class_index<=class_num;class_index++)
//            if (probabilities_sum[class_index]>second_highest_prob && class_index!=predicted_class) 
//            {  second_highest_prob=probabilities_sum[class_index];
//               second_highest_class=class_index;
//            }
//          testset_samples[test_sample_index]->interpolated_value=(second_highest_prob*atof(class_labels[second_highest_class].c_str())+probabilities_sum[predicted_class]*atof(class_labels[predicted_class].c_str()))/(second_highest_prob+probabilities_sum[predicted_class]);
          // Method 2: use all the marginal probabilities
			testset_samples[test_sample_index]->interpolated_value=0;			
			for( class_index = 1; class_index <= class_num; class_index++ )
				testset_samples[ test_sample_index ]->interpolated_value += 
					probabilities_sum[class_index] * atof( class_labels[ class_index ].c_str() );

		}
		if (do_html) sprintf(interpolated_value,"<td>%.3g</td>",testset_samples[test_sample_index]->interpolated_value);
	} else if (do_html) strcpy(interpolated_value,"");

	if (do_html) {
		if (closest_sample) sprintf(closest_image,"<td><A HREF=\"%s\"><IMG WIDTH=40 HEIGHT=40 SRC=\"%s__1\"></A></td>",closest_sample->full_path,closest_sample->full_path);
		else strcpy(closest_image,"");
	}

	if (is_continuous) {
		if (sample_class) { // known class
			if (do_html) sprintf(buffer,"<td></td><td>%.3g</td><td>%.3f</td>",testset_samples[test_sample_index]->sample_value,testset_samples[test_sample_index]->interpolated_value);
			// if a known class, print actual value,predicted value, percent error(abs((actual-predicted)/actual)).
			if (verbosity>=1)
				printf("%f\t%f\t%f\n",testset_samples[test_sample_index]->sample_value,
					testset_samples[test_sample_index]->interpolated_value,
					fabs((testset_samples[test_sample_index]->sample_value-testset_samples[test_sample_index]->interpolated_value)/testset_samples[test_sample_index]->sample_value));
		} else { // Unknown class
			if (do_html) sprintf(buffer,"<td></td><td>UNKNOWN</td><td>%.3g</td>",testset_samples[test_sample_index]->interpolated_value);
			// if a known class, print actual value,predicted value, percent error(abs((actual-predicted)/actual)).  Otherwise just predicted value.
			if (verbosity>=1)
				printf("N/A\t%f\n",testset_samples[test_sample_index]->interpolated_value);
		}
	} else { // discrete classes
	// if a known class, print actual class,predicted class.  Otherwise just predicted value.
		if (sample_class) { // known class
			if (verbosity>=1) {
				printf("%s\t%s",class_labels[sample_class].c_str(),class_labels[predicted_class].c_str());
				if (interpolate) printf ("\t%.3f",testset_samples[ test_sample_index ]->interpolated_value);
				printf("\n");
			}
			if (do_html) sprintf(buffer,"<td></td><td>%s</td><td>%s</td><td>%s</td>%s",class_labels[sample_class].c_str(),class_labels[predicted_class].c_str(),color,interpolated_value);
		} else {
			if (verbosity>=1) {
				std::cout << TestSet->class_labels[ TestSet->samples[ test_sample_index ]->sample_class ] << "*\t" << class_labels[ predicted_class ];
				if (interpolate) printf ("\t%.3f",testset_samples[ test_sample_index ]->interpolated_value);
				printf("\n");
			}
			if (do_html)
				sprintf(buffer,"<td></td><td>%s*</td><td>%s</td><td>%s</td>%s",
					TestSet->class_labels[TestSet->samples[ test_sample_index ]->sample_class].c_str(),
					class_labels[predicted_class].c_str(),
					color,
					interpolated_value);
		}
	}
	if (do_html) {
		strcat(one_image_string,buffer);
		sprintf(buffer,"<td><A HREF=\"%s\"><IMG WIDTH=40 HEIGHT=40 SRC=\"%s__1\"></A></td>%s</tr>\n",testset_samples[test_sample_index]->full_path,testset_samples[test_sample_index]->full_path,closest_image); /* add the links to the image */
		strcat(one_image_string,buffer);
		strcat(split->individual_images,one_image_string);   /* add the image to the string */
	}

   /* end of reporting */

   if (similarities) for (class_index=1;class_index<=class_num;class_index++) similarities[class_index]=probabilities_sum[class_index];
   if (is_continuous) return(value);
   else return(predicted_class);
}

/* Test
   Test the classification accuracy using two sets of signatures
   method -int- 0 - WNN,   1 - WND-5
   split -*data_split- a pointer to a data split structure which contains the similarity matrix, confusion matrix, report string, feature_names, etc. ignored if NULL.
   tiles -int- number of tiles of each image.
   rank -long- the number of first closest classes among which a presence of the right class is considered a match
   max_tile -int- use only the most similar tile
*/
double TrainingSet::Test(TrainingSet *TestSet, int method, int tiles, int tile_areas, TrainingSet *TilesTrainingSets[], int max_tile,long rank, data_split *split)
{  int test_sample_index,n_test_samples,class_index,b;//tile_index;
   long accurate_prediction=0, known_images=0;//,interpolate=1;
	double value;
	int predicted_class;

   if (tiles<1) tiles=1;       /* make sure the number of tiles is at least 1 */
   if (rank<=0) rank=1;  /* set a valid value to rank                */
   if (split && split->individual_images) strcpy(split->individual_images,"");    /* make sure the string is initially empty */
           
   /*initialize the confusion and similarity matrix */
   if (split && split->confusion_matrix)
     for (class_index=0;class_index<(class_num+1)*(class_num+1);class_index++) split->confusion_matrix[class_index]=0;
   if (split && split->similarity_matrix)
     for (class_index=0;class_index<(class_num+1)*(class_num+1);class_index++) split->similarity_matrix[class_index]=0.0;
   if (split && split->class_probability_matrix)
     for (class_index=0;class_index<(class_num+1)*(class_num+1);class_index++) split->class_probability_matrix[class_index]=0.0;
   if (split && split->image_similarities)
     for (class_index=0;class_index<(TestSet->count/tiles+1)*(TestSet->count/tiles+1);class_index++) split->image_similarities[class_index]=0.0;

   // perform the actual test
 
#if( DEBUG_CREATE_INDIV_DISTANCE_FILES) 
	  if( method == WND ) {
		 // These are files that contain distances and similarities for individual distances
		 // They are printed into by classify2()
		 // Initialize them by truncating them.
		 std::ofstream indiv_dists_file ( "individual_distances.csv", std::ios::trunc );
		 indiv_dists_file.close();

		 std::ofstream indiv_simls_file ( "individual_similarities.csv", std::ios::trunc );
		 indiv_simls_file.close();
		 
		 std::ofstream class_report ( "class_dists_and_simls.txt", std::ios::trunc );
		 class_report.close();
	 }
#endif

// If its a pure testset, use the samples vector instead of test_samples
	std::vector<signatures *> &testset_samples = TestSet->test_samples.size() > 0 ? TestSet->test_samples : TestSet->samples;
	n_test_samples = testset_samples.size();
	 for( test_sample_index = 0; test_sample_index < n_test_samples; test_sample_index += tiles )
	 {
		 if( is_continuous )
			 value = ClassifyImage (TestSet,test_sample_index,method,tiles,tile_areas,TilesTrainingSets,max_tile,rank,split,NULL);
		   //FIXME: do what with the value in "value"?
		 else 
		 {
			 predicted_class = int( ClassifyImage( TestSet, test_sample_index, method, tiles, tile_areas, TilesTrainingSets, max_tile, rank, split, NULL ) );
			 if( testset_samples[ test_sample_index ]->sample_class )
			 {
				 known_images++;
				 if( predicted_class == testset_samples[ test_sample_index ]->sample_class )
					 accurate_prediction++;
			 }
		 }
	 }
/*
  normalize the similarity matrix
  Method: The similarity matrix now contains the sum of marginal probabilities for each class.
    The number of known test samples is the sum of the row for each class in the confusion matrix.
    The normalization is simply to divide each cell in the similarity matrix by the sum of the corresponding row in the confusion matrix.
*/
	if (split && split->similarity_matrix) {
		double P = 0, choose;
	 	split->known_images = 0;
	 	split->accurate_predictions = 0;
		for (class_index=1;class_index<=class_num;class_index++) {
			double class_sim;
			int class_test_samples=0;
     	// Get the number of known test samples
			for (b=1;b<=class_num;b++)
				class_test_samples+=split->confusion_matrix[class_num*class_index+b];
			split->known_images += class_test_samples;
			class_sim=split->similarity_matrix[class_num*class_index+class_index]/class_test_samples;
			split->accurate_predictions += split->confusion_matrix[class_num*class_index+class_index];
			split->class_accuracies[class_index] = (double)split->confusion_matrix[class_num*class_index+class_index] / (double)class_test_samples;
			for (b=1;b<=class_num;b++) {
				split->class_probability_matrix[class_num*class_index+b] =
					split->similarity_matrix[class_num*class_index+b] / class_test_samples;
				split->similarity_matrix[class_num*class_index+b] /= (class_test_samples*class_sim);
			}
		}
	// Calculate the per-class accuracy statistics
		split->accuracy = (double)split->accurate_predictions / (double)split->known_images;
		double plus_minus = 0, avg_class_accuracies = 0;
		for (class_index=1;class_index<=class_num;class_index++) {
			if (fabs(split->class_accuracies[class_index] - split->accuracy) > plus_minus) plus_minus = fabs(split->class_accuracies[class_index] - split->accuracy);
			avg_class_accuracies += split->class_accuracies[class_index];
		}
		split->plus_minus = plus_minus;
		split->avg_class_accuracies = (double)avg_class_accuracies / (double)class_num;

	// Find the P-value
		for (int correct = split->accurate_predictions; correct <= split->known_images; correct++)  /* find the P */
		// gsl_sf_choose (n,m) = n!/(m!(n-m)!)
			if ( gsl_sf_choose (split->known_images,correct,&choose) == GSL_SUCCESS )
				P += pow( ( 1/(double)class_num ), correct ) * pow( 1-1/(double)class_num, int( split->known_images ) - correct ) * choose; // compiler complained about long int - CEC 6/7/11
		split->classification_p_value = P;
	}

   /* normalize the image similarities */
   if (split && split->image_similarities)
   {  double min_dist=INF,max_dist=0.0;
      /* subtract the minimum distance */
      for (test_sample_index=0;test_sample_index<TestSet->count;test_sample_index+=tiles)
        for (b=0;b<TestSet->count;b++)
          if (split->image_similarities[(1+test_sample_index/tiles)*(TestSet->count/tiles+1)+b/tiles+1]>0 && split->image_similarities[(1+test_sample_index/tiles)*(TestSet->count/tiles+1)+b/tiles+1]<min_dist) min_dist=split->image_similarities[(1+test_sample_index/tiles)*(TestSet->count/tiles+1)+b/tiles+1]; 
      for (test_sample_index=0;test_sample_index<TestSet->count;test_sample_index+=tiles)
        for (b=0;b<TestSet->count;b+=tiles)
          split->image_similarities[(1+test_sample_index/tiles)*(TestSet->count/tiles+1)+b/tiles+1]-=min_dist;
      /* divide by the maximal distance */
      for (test_sample_index=0;test_sample_index<TestSet->count;test_sample_index+=tiles)
        for (b=0;b<TestSet->count;b+=tiles)
           if (split->image_similarities[(1+test_sample_index/tiles)*(TestSet->count/tiles+1)+b/tiles+1]>max_dist) max_dist=split->image_similarities[(1+test_sample_index/tiles)*(TestSet->count/tiles+1)+b/tiles+1];
      for (test_sample_index=0;test_sample_index<TestSet->count;test_sample_index+=tiles)
        for (b=0;b<TestSet->count;b+=tiles)
          split->image_similarities[(1+test_sample_index/tiles)*(TestSet->count/tiles+1)+b/tiles+1]/=max_dist;
   }

   if (is_continuous) return(0);   /* no classification accuracy if continouos values are used */
	 return( split->accuracy);
}


/* normalize
   normalize the signature in the training set to the interval [0,100]
*/

void TrainingSet::normalize() {  
	int class_index;

	Eigen::MatrixXd class_max (signature_count,class_num+1),class_min (signature_count,class_num+1);
	for (class_index=0;class_index<=class_num;class_index++) {
		Eigen::MatrixXd &raw_features_ref = raw_features[class_index];
		if (raw_features_ref.rows() < 1) {
			class_max.col(class_index).setConstant (-DBL_MAX);
			class_min.col(class_index).setConstant (DBL_MAX);
			continue;
		}
	// First, clip the values to between DBL_MAX and -DBL_MAX
		raw_features_ref = (raw_features_ref.array() < DBL_MAX).select(raw_features_ref, DBL_MAX);
		raw_features_ref = (raw_features_ref.array() > -DBL_MAX).select(raw_features_ref, -DBL_MAX);

	// Then, get the mins and maxes for each class
		class_max.col(class_index) = raw_features_ref.rowwise().maxCoeff();
		class_min.col(class_index) = raw_features_ref.rowwise().minCoeff();
	}
// Get the mins and maxes for all features, and store them for later scaling
	SignatureMaxes = class_max.rowwise().maxCoeff();
	SignatureMins  = class_min.rowwise().minCoeff();
	SignatureRanges = SignatureMaxes - SignatureMins;

	for (class_index=0;class_index<=class_num;class_index++) {
		Eigen::MatrixXd &raw_features_ref = raw_features[class_index];
		if (raw_features_ref.rows() < 1) continue;
		for (int sample_index = 0; sample_index < raw_features_ref.cols(); sample_index++) {
			raw_features_ref.col(sample_index) = (SignatureRanges.array() > DBL_EPSILON).select (((raw_features_ref.col(sample_index) - SignatureMins).array() / SignatureRanges.array()) * 100, 0);
		}
	}
}

void TrainingSet::SetmRMRScores(double used_signatures, double used_mrmr)
{  FILE *mrmr_file;
   char buffer[512],*p_buffer; 
   int sig_index,sample_index;

   /* use mRMR (if an executable file "mrmr" exists) */
   if ( (mrmr_file=fopen("mrmr","r")) ) fclose(mrmr_file);
   if (mrmr_file)  /* use mrmr */
   {  /* first create a csv file for mrmr */
      mrmr_file=fopen("mrmr_sigs.csv","w");
      fprintf(mrmr_file,"class");
	  for (sig_index=0;sig_index<signature_count;sig_index++)
         if (SignatureWeights[sig_index]>0) fprintf(mrmr_file,",%d",sig_index);
      fprintf(mrmr_file,"\n");
      for (sample_index=0;sample_index<count;sample_index++)
      {  fprintf(mrmr_file,"%d",samples[sample_index]->sample_class);
	     for (sig_index=0;sig_index<signature_count;sig_index++)
           if (SignatureWeights[sig_index]>0) fprintf(mrmr_file,",%.0f",samples[sample_index]->data[sig_index].value);
         fprintf(mrmr_file,"\n");
      }
	  fclose(mrmr_file);
      sprintf(buffer,"./mrmr -i mrmr_sigs.csv -n %ld -s %ld -v %ld > mrmr_output",(long)(used_mrmr*used_signatures*signature_count),count,signature_count);
      printf("%s\n",buffer);
	  system(buffer);	  
      remove("mrmr_sigs.csv");
      /* now read the mRMR output file */
	  for (sig_index=0;sig_index<signature_count;sig_index++)  /* first set all scores to zero */
         SignatureWeights[sig_index]=0.0;	  
      if (!(mrmr_file=fopen("mrmr_output","r"))) printf("Cannot open file 'mrmr_sigs.csv'\n");
	  p_buffer=fgets(buffer,sizeof(buffer),mrmr_file); /* skip the first lines */
      while (p_buffer && strstr(p_buffer,"mRMR")==NULL) p_buffer=fgets(buffer,sizeof(buffer),mrmr_file);
      if (!p_buffer) printf("Cannot parse file 'mrmr_output'\n");	  
	  p_buffer=fgets(buffer,sizeof(buffer),mrmr_file); /* skip the first line */	  
	  p_buffer=fgets(buffer,sizeof(buffer),mrmr_file); /* skip the first line */	  	  
      while(p_buffer && strlen(p_buffer)>8)
	  {  long sig_num;
         double weight;
	     strtok(p_buffer," \t\n");
         strtok(NULL," \t\n");		 
         sig_num=atoi(strtok(NULL," \t\n"));
        weight=atof(strtok(NULL," \t\n"));
        if (weight<0) weight=0.0;   /* make sure the values are not negative */
		if (weight>0) SignatureWeights[sig_num]=pow(weight,1);
         p_buffer=fgets(buffer,sizeof(buffer),mrmr_file);	  
	  }
	  fclose(mrmr_file);
      remove("mrmr_output");	 
   }   
}

/* SetFisherScores
   Compute the fisher score of each signature
   used_signatures -double- what fraction of the signatures should be used (a value between 0 and 1).
   sorted_feature_names -char *- a text of the names and scores of the features (NULL to ignore)
   int method - 0 for Fisher Scores. 1 for Pearson Correlation scores (with the ground truth).
*/


void TrainingSet::SetFisherScores(double used_signatures, double used_mrmr, data_split *split) {
	int sample_index,sig_index,class_index;
	//	double mean,var,class_dev_from_mean,mean_inner_class_var;
	double threshold;   

	// Make a featuregroup map and iterator
	UNORDERED_MAP<std::string, featuregroup_stats_t> featuregroups;
	UNORDERED_MAP<std::string, featuregroup_stats_t>::iterator fg_it;

	// An object instance to collect the stats for each feature + group
	featuregroup_stats_t featuregroup_stats;
	feature_stats_t feature_stats;
  
	// And an object instance of FeatureNames' FeatureInfo class, which has broken-down info about each feature type.
	FeatureInfo const *featureinfo;

	if (split)
	{
		split->feature_stats.clear();
		split->featuregroups_stats.clear();
	}


// 
//    /* use Fisher scores (for classes) or correlation scores (for correlations) */
// 	for (sig_index=0;sig_index<signature_count;sig_index++)  {
// 		if (class_num>0) {  /* Fisher Scores */
// 		// initialize
// 			class_mean.setZero  ();
// 			class_var.setZero   ();
// 			class_count.setZero ();
// 
// 			mean=var=0.0;
// 		// find the mean for each class
// 			for (class_index=1;class_index<=class_num;class_index++) {
// 				Eigen::MatrixXd &raw_features_ref = raw_features[sample_class];
// 				class_mean = raw_features_ref.rowwise().mean();
// 				class_var = ( raw_features_ref - class_mean.replicate(raw_features_ref.cols()) ).rowwise().square().mean();
// 			}
// 		
//          for (sample_index=0;sample_index<count;sample_index++)
//          {  class_mean[samples[sample_index]->sample_class]+=samples[sample_index]->data[sig_index].value;
//             class_count[samples[sample_index]->sample_class]+=1;
//          }
// 
//          for (class_index=1;class_index<=class_num;class_index++)
//            if (class_count[class_index])
//              class_mean[class_index]/=class_count[class_index];
// 
//          /* find the variance */
//          for (sample_index=0;sample_index<count;sample_index++)
//            class_var[samples[sample_index]->sample_class]+=pow(samples[sample_index]->data[sig_index].value-class_mean[samples[sample_index]->sample_class],2);
// 
//          for (class_index=1;class_index<=class_num;class_index++)
//            if (class_count[class_index])
//              class_var[class_index]/=class_count[class_index];
// 
//          /* compute fisher score */
// 
//          /* find the mean of all means */
//          for (class_index=1;class_index<=class_num;class_index++)
//            mean+=class_mean[class_index];
//          mean/=class_num;
//          /* find the variance of all means */
//          class_dev_from_mean=0;
//          for (class_index=1;class_index<=class_num;class_index++)
//            class_dev_from_mean+=pow(class_mean[class_index]-mean,2);
//          if (class_num>1) class_dev_from_mean/=(class_num-1);
// 	     else class_dev_from_mean=0;
// 
//          mean_inner_class_var=0;
//          for (class_index=1;class_index<=class_num;class_index++)
//            mean_inner_class_var+=class_var[class_index];
//          mean_inner_class_var/=class_num;
//          if (mean_inner_class_var==0) mean_inner_class_var+=0.000001;   /* avoid division by zero - and avoid INF values */
// 
//          SignatureWeights[sig_index]=class_dev_from_mean/mean_inner_class_var;
///*
//char *p1,*p2;
//p1=strrchr(SignatureNames[sig_index],'#');
//p2=strrchr(SignatureNames[sig_index],'_');
//if (!p1) SignatureWeights[sig_index]=0;
//if (p1) if (((long)p2-(long)p1)>3) SignatureWeights[sig_index]=0;
//*/		 

//if (strchr(SignatureNames[sig_index],'(') && (strstr(SignatureNames[sig_index],"()")==NULL)) SignatureWeights[sig_index]=0;
//if ( (1)
//&& (strstr(SignatureNames[sig_index],"lick")==NULL)
//&& (strstr(SignatureNames[sig_index],"oment")==NULL) 
//&& (strstr(SignatureNames[sig_index],"dge")==NULL) 
//&& (strstr(SignatureNames[sig_index],"ernike")==NULL) 
//&& (strstr(SignatureNames[sig_index],"eature")==NULL) 
//&& (strstr(SignatureNames[sig_index],"amura")==NULL) 
//&& (strstr(SignatureNames[sig_index],"abor")==NULL) 
//&& (strstr(SignatureNames[sig_index],"istogram")==NULL) 
//&& (strstr(SignatureNames[sig_index],"hebyshev")==NULL) 
//&& (strstr(SignatureNames[sig_index],"adon")==NULL) 
//) SignatureWeights[sig_index]=0;
//if (SignatureWeights[sig_index]>0) printf("%s\n",SignatureNames[sig_index]);
// 
//       }  /* end of method 0 (Fisher Scores) */
	if (class_num > 1) {
		Eigen::MatrixXd class_mean (signature_count,class_num),class_var (signature_count,class_num);
		Eigen::MatrixXd class_delta;
		Eigen::VectorXd mean_class_means, mean_class_var, mean_inter_class_var;
		for (class_index=1;class_index<=class_num;class_index++) {
			Eigen::MatrixXd &raw_features_ref = raw_features[class_index];
			class_mean.col(class_index-1) = raw_features_ref.rowwise().mean();
			class_var.col(class_index-1) = (raw_features_ref.colwise() - class_mean.col(class_index-1)).array().square().matrix().rowwise().mean();
		}
	
		mean_class_means = class_mean.rowwise().mean();
	
		mean_class_var = ( class_mean.colwise() - mean_class_means ).array().square().matrix().rowwise().sum();
		mean_class_var /= class_num-1;
	
		mean_inter_class_var = class_var.rowwise().mean();
		mean_inter_class_var = (mean_inter_class_var.array() < DBL_EPSILON).select (DBL_EPSILON, mean_inter_class_var);

		SignatureWeights = mean_class_var.array() / mean_inter_class_var.array();
	}

 	for (sig_index=0;sig_index<signature_count;sig_index++)  {
      /* Pearson Correlation scores */
      if (is_continuous)
      {  double mean_ground=0,stddev_ground=0,mean=0,stddev=0,z_score_sum=0;
         for (sample_index=0;sample_index<count;sample_index++)  /* compute the mean of the continouos values */
           mean_ground+=(samples[sample_index]->sample_value/((double)count));
         for (sample_index=0;sample_index<count;sample_index++)  /* compute the stddev of the continouos values */
           stddev_ground+=pow(samples[sample_index]->sample_value-mean_ground,2);	  
         stddev_ground=sqrt(stddev_ground/count);
         for (sample_index=0;sample_index<count;sample_index++)
           mean+=(samples[sample_index]->data[sig_index].value/((double)count));
         for (sample_index=0;sample_index<count;sample_index++)  /* compute the stddev of the continouos values */
           stddev+=pow(samples[sample_index]->data[sig_index].value-mean,2);	  
         stddev=sqrt(stddev/count);	
         for (sample_index=0;sample_index<count;sample_index++)
           if (stddev>0 && stddev_ground>0) z_score_sum+=((samples[sample_index]->sample_value-mean_ground)/stddev_ground)*((samples[sample_index]->data[sig_index].value-mean)/stddev);
         SignatureWeights[sig_index]=pow(fabs(z_score_sum/count),1);
//printf("%d Fisher Score: %f\n",class_num,SignatureWeights[sig_index]);		 
	  } /* end of method 1 (Pearson Correlation) */


		//std::cout << sig_index << "  " << SignatureNames[sig_index];// << std::endl;
		// add the sums of the scores of each group of features
		// Get feature information from the name and store the feature and group name in our maps
		FeatureNames* FN_instance = FeatureNames::get_instance();
		featureinfo = FN_instance->getFeatureInfoByName ( SignatureNames[ sig_index ] );
		featureinfo->print_info();
		//cout << endl;
	// find it in our map by name
		fg_it = featuregroups.find(featureinfo->group->name);
	// if its a new feature group, initialize a stats structure, and add it to our map
		if (fg_it == featuregroups.end()) {
			featuregroup_stats.name = featureinfo->group->name;
			featuregroup_stats.featuregroup_info = featureinfo->group;
			featuregroup_stats.sum_weight = SignatureWeights[ sig_index ];
			featuregroup_stats.sum_weight2 = (SignatureWeights[ sig_index ] * SignatureWeights[ sig_index ]);
			featuregroup_stats.min = SignatureWeights[ sig_index ];
			featuregroup_stats.max = SignatureWeights[ sig_index ];
			featuregroup_stats.mean = 0;
			featuregroup_stats.stddev = 0;
			featuregroup_stats.n_features = 1;
			featuregroups[featureinfo->group->name] = featuregroup_stats; // does a copy
		}
		else
		{
			if (SignatureWeights[ sig_index ] < fg_it->second.min)
				fg_it->second.min = SignatureWeights[ sig_index ];
			if (SignatureWeights[ sig_index ] > fg_it->second.max)
				fg_it->second.max = SignatureWeights[ sig_index ];
			fg_it->second.sum_weight += SignatureWeights[ sig_index ];
			fg_it->second.sum_weight2 += (SignatureWeights[ sig_index ] * SignatureWeights[ sig_index ]);
			fg_it->second.n_features++;
		}
		
		
		// Initialize a feature stats structure, and add it to our vector
		feature_stats.name = SignatureNames[ sig_index ];
		feature_stats.feature_info = featureinfo;
		feature_stats.weight = SignatureWeights[ sig_index ];
		feature_stats.index = sig_index;
		split->feature_stats.push_back (feature_stats); // makes a copy
	} // END iterating over all signatures

	// Feature group sorting code:
	// Copy the map to the vector while updating summary stats
	split->featuregroups_stats.clear();
	for(fg_it = featuregroups.begin(); fg_it != featuregroups.end(); ++fg_it )
	{
		fg_it->second.mean = fg_it->second.sum_weight / (double)(fg_it->second.n_features);
		fg_it->second.stddev = sqrt (
			(fg_it->second.sum_weight2 - (fg_it->second.sum_weight * fg_it->second.mean)) / (double)(fg_it->second.n_features - 1)
			); // sqrt (variance) for stddev
		split->featuregroups_stats.push_back (fg_it->second);
	}
	// Sort the featuregroup vector by mean weight
	sort_by_mean_weight_t sort_by_mean_weight_func;
	sort (split->featuregroups_stats.begin(), split->featuregroups_stats.end(), sort_by_mean_weight_func);

	// Sort the features in our split by weight, and use the sorted list to get the threshold value
	sort_by_weight_t sort_by_weight_func;
	sort (split->feature_stats.begin(), split->feature_stats.end(), sort_by_weight_func);
	int last_index = (int)floor( (used_signatures * (double)signature_count) + 0.5 );
	threshold=split->feature_stats[last_index].weight;
	// Set the SignatureWeights to 0 wherever its less than threshold
	for (sig_index = last_index+1; sig_index < signature_count; sig_index++)
		SignatureWeights[ split->feature_stats[sig_index].index ] = 0.0;
	// Lop off the vector after the threshold
	split->feature_stats.erase (split->feature_stats.begin() + last_index + 1,split->feature_stats.end());

	// now set up the projected_features based on the feature_stats vector
	int n_kept_sigs=split->feature_stats.size();
	ReducedFeatureIndexes.resize (n_kept_sigs);
	ReducedFeatureWeights2.resize (n_kept_sigs);
	
	for (sig_index = 0; sig_index < n_kept_sigs; sig_index++) {
	ReducedFeatureIndexes[sig_index] = split->feature_stats[sig_index].index;
		ReducedFeatureWeights2[sig_index] = pow (split->feature_stats[sig_index].weight,2);
	}

	for (class_index = 0; class_index <= class_num; class_index++) {
		if (raw_features[class_index].cols()) {
			projected_features[class_index].resize(n_kept_sigs,raw_features[class_index].cols());
			for (sig_index=0; sig_index < n_kept_sigs; sig_index++) {
				projected_features[class_index].row(sig_index) = raw_features[class_index].row(ReducedFeatureIndexes[sig_index]);
			}
		}
	}


	if (used_mrmr>0) SetmRMRScores(used_signatures,used_mrmr);  /* filter the most informative features using mrmr */
}


/* IgnoreFeatureGroup
   classify without using one of the feature groups (identified by 'index'). This function is used for assessing the contribution of the different image features.
   index -long- the index (in the group order) of the group to be ignored
   group_name -char *- the name of the ignored group. This output variable is ignored if NULL.
*/
int TrainingSet::IgnoreFeatureGroup(long index,char *group_name)
{  int group=0,sig_index=0;
	size_t char_index;
   char current_name[256]={'\0'},last_name[256]={'\0'};
   
   while(group<=index)
   {  if (sig_index>=signature_count) return(0);   /* no more image features */
      while (SignatureNames[sig_index][0]<'A' || SignatureNames[sig_index][0]>'Z') sig_index++;
      strcpy(current_name,SignatureNames[sig_index]);
      if (strchr(current_name,' ')) *(strchr(current_name,' '))='\0';
      if (strcmp(current_name,last_name)!=0) group++;
	  strcpy(last_name,current_name);
	  if (group==index) 
	  {  SignatureWeights[sig_index]=0;
         if (group_name) strcpy(group_name,SignatureNames[sig_index]);   /* return the name of the group */	  
         for (char_index=0;char_index<strlen(group_name);char_index++) if (isdigit(group_name[char_index])) group_name[char_index]=' ';          
      }
	  sig_index++;
   }
   return(1);
}

/* distance 
   Find the weighted Euclidean distance between two samples
*/
double TrainingSet::distance(signatures *sample1, signatures *sample2, double power)
{   double dist=0;
    int sig_index;	
      for (sig_index=0;sig_index<signature_count;sig_index++)
        dist=dist+pow(SignatureWeights[sig_index],1)*pow(sample1->data[sig_index].value-sample2->data[sig_index].value,power);
    return(pow(dist,1/power));
}

/* WNNclassify
   classify a given sample using weighted nearest neioghbor
   test_sample -signature *- a given sample to classify
   probabilities -array of double- an array (size num_classes) marginal probabilities of the given sample from each class. (ignored if NULL).
   normalization_factor -double *- the normalization factor used to compute the marginal probabilities from the distances normalization_factor=1/(sum_dist*marginal_prob). ignored if NULL.
   closest_sample -signatures **- a pointer to the closest sample found. (ignored if NULL).
   returned value -long- the predicted class of the sample

   comment: must set weights before calling to this function
*/
long TrainingSet::WNNclassify(signatures *test_sample, double *probabilities, double *normalization_factor,signatures **closest_sample)
{  int class_index,sample_index;
   long most_probable_class=0;
   double closest_dist=INF;
	Eigen::VectorXd sample_vec;

   /* initialize the probabilities */
   if (probabilities)
     for (class_index=0;class_index<=class_num;class_index++)
        probabilities[class_index]=INF;

   /* normalize the test sample */
   test_sample->normalize(this, sample_vec);
   for (sample_index=0;sample_index<count;sample_index++)
   {  double dist=distance(test_sample,samples[sample_index],2.0);
      if ((dist<1/INF) || (strcmp(samples[sample_index]->full_path,test_sample->full_path)==0)) dist=INF;    /* ignore images that are 100% identical */
//if (strstr(samples[sample_index]->full_path,"1948")==NULL) dist=INF;	  
      if (dist<closest_dist)
      {  closest_dist=dist;
         most_probable_class=samples[sample_index]->sample_class;
         if (closest_sample) *closest_sample=samples[sample_index];		 
      }
      /* set the distance from classes */
      if (probabilities)
        if (dist<probabilities[samples[sample_index]->sample_class])
          probabilities[samples[sample_index]->sample_class]=dist;
   }
    
   /* normalize the marginal probabilities */
   if (probabilities)
   {  double sum_dists=0;
      for (class_index=1;class_index<=class_num;class_index++)
        if (probabilities[class_index]!=0)
          sum_dists+=1/probabilities[class_index];
      for (class_index=1;class_index<=class_num;class_index++)
        if (sum_dists==0) probabilities[class_index]=0;    /* protect from division by zero */
        else
          if (probabilities[class_index]==0) probabilities[class_index]=1.0; /* exact match */
          else probabilities[class_index]=(1/probabilities[class_index])/sum_dists;
      if (normalization_factor) *normalization_factor=sum_dists;
   }

   return(most_probable_class);
}


/* classify2
   classify a given sample
   test_sample -signature *- a given sample to classify
   probabilities -array of double- an array (size num_classes) marginal probabilities of the given sample from each class. (ignored if NULL).
   normalization_factor -double *- the normalization factor used to compute the marginal probabilities from the distances normalization_factor=1/(dist*marginal_prob). Ignored if NULL.   
   returned value -long- the predicted class of the sample

   comment: must set weights before calling to this function
		CEC: also, what is the purpose of the argument char* name? 
*/
long TrainingSet::classify2(char* name, int test_sample_index, signatures *test_sample, double *probabilities, double *normalization_factor) { 
	using namespace std;
	vector<int> num_samples_per_class( class_num + 1, 0 ); 
	vector<double> indiv_distances( count, 0.0 ); 
	vector<double> indiv_similarities( count, 0.0 ); 
// class numberings start at 1.
	vector<double> class_similarities( class_num + 1, 0.0 );
	vector<double> class_distances( class_num + 1, 0.0 );
	vector<int> num_collisions( class_num + 1, 0 );

	/* normalize the test sample */
	Eigen::VectorXd sample_vec, weight_vec;
	test_sample->normalize(this,sample_vec);

	int class_index, sample_index, n_samples;
	double dist;
	for (class_index = 1; class_index <= class_num; class_index++) {
		n_samples = raw_features[class_index].cols();
		num_samples_per_class[ class_index ] = class_distances[ class_index ] = class_similarities[ class_index ] = 0;
		for (sample_index = 0; sample_index < n_samples; sample_index++) {
			Eigen::VectorXd dist_vec = projected_features[class_index].col(sample_index) - sample_vec;
			dist_vec = (dist_vec.array().abs() < DBL_EPSILON).select(0, dist_vec.array().square() * ReducedFeatureWeights2.array());

			dist = dist_vec.sum();
			if (dist > DBL_EPSILON) {
				num_samples_per_class[ class_index ]++;
				class_distances[ class_index ] += dist;
				class_similarities[ class_index ] += pow (dist,-5);
			}
		}
//std::cout << "num_samples_per_class[ class_index ]: " << num_samples_per_class[ class_index ] << "\n";
//std::cout << "class_distances[ class_index ]: " << class_distances[ class_index ] << "\n";
//std::cout << "class_similarities[ class_index ]: " << class_similarities[ class_index ] << "\n";
	}


  long most_probable_class = -1;
  double max_similarity = 0;

  for( class_index = 1; class_index <= class_num; class_index++ )
  {
    if( num_samples_per_class[ class_index ] == 0 )
      continue;
		else
		{
			class_distances[ class_index ] /= num_samples_per_class[ class_index ];
			class_similarities[ class_index ] /= num_samples_per_class[ class_index ];
		}
    // printf( "Dist to class %d = %e = %e class_sum / %d samples\n", class_index, class_sum[class_index]/=samples_num[class_index], class_sum[class_index], samples_num[class_index] );

    if( class_similarities[ class_index ] > max_similarity )
    {
      max_similarity = class_similarities[ class_index ];
      most_probable_class = class_index;
    }
  }

  // normalize the marginal probabilities
  if (probabilities)
  {
    double sum_dists=0;

    //printf( "\n\n" );

    for( class_index = 1; class_index <= class_num; class_index++ )
      sum_dists += class_similarities[ class_index ];

    // printf( "Sum of all distances = %e\n", sum_dists );

    for( class_index = 1; class_index <= class_num; class_index++ )
      probabilities[ class_index ]= class_similarities[ class_index ] / sum_dists;

    if( normalization_factor )
			*normalization_factor = sum_dists;
  }

#if DEBUG_CREATE_INDIV_DISTANCE_FILES
		// Looking at how similarities and distances behave

		ofstream indiv_dists_file ( "individual_distances.csv", ios::app );
		ofstream indiv_simls_file ( "individual_similarities.csv", ios::app );
		ofstream class_report ( "class_dists_and_simls.txt", ios::app );

		vector<double>::iterator it;
		vector<int>::iterator iit;

		indiv_dists_file << test_sample->full_path << ",";
		indiv_dists_file.precision(5);
		indiv_dists_file << scientific;
		for( it = indiv_distances.begin(); it != indiv_distances.end(); it++ )
			indiv_dists_file << *it << ",";
		indiv_dists_file << endl;
		indiv_dists_file.close();

		indiv_simls_file << test_sample->full_path << ",";
		indiv_simls_file.precision(5);
		indiv_simls_file << scientific;
		for( it = indiv_similarities.begin(); it != indiv_similarities.end(); it++ )
			indiv_simls_file << *it << ",";
		indiv_simls_file << endl;
		indiv_simls_file.close();

		class_report << "Image " << test_sample_index << " "<< test_sample->full_path << ", predicted: "
		             << most_probable_class << ", ground truth: " << samples[ test_sample_index ]->sample_class << endl;
		class_report.precision(5);
		class_report << scientific;
		//class zero is the unknown class, don't use it here
		for( it = class_distances.begin() + 1; it != class_distances.end(); it++)
			class_report << *it << "\t";
		class_report << endl;
		for( it = class_similarities.begin() + 1; it != class_similarities.end(); it++ )
			class_report << *it << "\t";
		class_report << endl;
		for( iit = num_collisions.begin() + 1; iit != num_collisions.end(); iit++ )
			class_report << *iit << "\t";
		class_report << endl << endl;
		for( iit = num_samples_per_class.begin() + 1; iit != num_samples_per_class.end(); iit++ )
			class_report << *iit << "\t";
		class_report << endl << endl;
		class_report.close();
  }
#endif

  return(most_probable_class);
}
/*
  {  int sample_index,class_index,sig_index;
   long most_probable_class;
   double samp_sum,*class_sum; //,*samples_num;
   double dist,closest_dist=INF;
   int *samples_num;
   double intermediate_result = 0;

   // normalize the test sample 
   test_sample->normalize(this);

   // allocate and initialize memory
   class_sum=new double[class_num+1];
   samples_num=new int[class_num+1];


   for (class_index=0;class_index<=class_num;class_index++)
   {  
     class_sum[class_index]=0.0;
     samples_num[class_index]=0;
   }

   for (sample_index=0;sample_index<count;sample_index++)
   {
     samp_sum=0.0;
     for (sig_index=0;sig_index<signature_count;sig_index++)
     {
         intermediate_result = pow( SignatureWeights[ sig_index ], 2 ) * 
           pow( test_sample->data[ sig_index ].value - 
             samples[ sample_index ]->data[ sig_index ].value, 2 );
         // Trying to prevent against accumulated floating point error
         if( intermediate_result > DBL_EPSILON )
           samp_sum += intermediate_result;
     }
     //if( samp_sum == 0.0 ) continue; // ignore images that are 100% identical 
     if( samp_sum < DBL_EPSILON ) continue; // try to weed out matches that got through due to floating point error 
     class_sum[samples[sample_index]->sample_class]+=pow(samp_sum,-5);
     samples_num[samples[sample_index]->sample_class]++;
     // printf( "\ttest img index %i, test img class: %i, dist w/o ^-5 %f, dist w/ ^-5 %e, class sum so far: %e, number of test images from this class seen so far: %d\n", sample_index, samples[sample_index]->sample_class, samp_sum, pow(samp_sum, -5), class_sum[samples[sample_index]->sample_class], samples_num[samples[sample_index]->sample_class]);
      
   }
	 
   for (class_index=1;class_index<=class_num;class_index++)
   {
     if( samples_num[class_index]==0 )
       class_sum[class_index]=INF;   // no samples for this class 
//     else
//       class_sum[class_index]/=samples_num[class_index];   // find the average distance per sample 
     
     // printf( "Dist to class %d = %e = %e class_sum / %d samples\n", class_index, class_sum[class_index]/=samples_num[class_index], class_sum[class_index], samples_num[class_index] );

     dist=class_sum[class_index];
     if( dist < closest_dist )
     {
       closest_dist=dist;
       most_probable_class=class_index;
     }
   }
   
   // normalize the marginal probabilities 
   if (probabilities)
   {  
     double sum_dists=0;

     //printf( "\n\n" );

     for( class_index = 1; class_index <= class_num; class_index++ )
       sum_dists += class_sum[class_index];

     // printf( "Sum of all distances = %e\n", sum_dists );

     for( class_index = 1; class_index <= class_num; class_index++ )
       probabilities[class_index]=class_sum[class_index]/sum_dists;
     
     if (normalization_factor) *normalization_factor=sum_dists;
   }

   delete class_sum;
   delete samples_num;
   return(most_probable_class);
}
*/

/* InterpolateValue
   Compute the interpolated value of a given test sample
   method -int- 0 for nearest neighbors, 1 for two closest samples
   N -int- number of neighbors to use
   closest_dist -double *- if not NULL holds the distance to the closest sample
*/
double TrainingSet::InterpolateValue(signatures *test_sample, int method, int N, signatures **closest_sample, double *closest_dist)
{  int sample_index,close_index;
   double *min_dists,*min_dists_values,val=0.0,sum=0.0;
// double min_dist_up=INF,min_dist_down=-INF,min_val_up,min_val_down;
	Eigen::VectorXd sample_vec;

   /* normalize the test sample */
   test_sample->normalize(this, sample_vec);
      
//   if (method==0)
   {  min_dists=new double[N];
      min_dists_values=new double[N];
      for (close_index=0;close_index<N;close_index++)
        min_dists[close_index]=INF;

      /* find the closest samples */
      for (sample_index=0;sample_index<count;sample_index++)
      {  double dist=distance(test_sample,samples[sample_index],2.0);
//printf("dist: %f   %f\n",dist,samples[sample_index]->sample_value);	  
         if (closest_sample && dist<min_dists[0]) *closest_sample=samples[sample_index];  /* for returning the closest sample */	  
         if (closest_dist && dist<min_dists[0]) *closest_dist=dist;                       /* for returning the distanmce to the closest sample */	  		 
         for (close_index=0;close_index<N;close_index++)
         if (dist<min_dists[close_index])
         {  memmove(&(min_dists[close_index+1]),&(min_dists[close_index]),sizeof(double)*(N-1-close_index));
            memmove(&(min_dists_values[close_index+1]),&(min_dists_values[close_index]),sizeof(long)*(N-1-close_index));
            min_dists[close_index]=dist;
            min_dists_values[close_index]=samples[sample_index]->sample_value;
//printf("%d %f %f %f %f\n",N,min_dists_values[0],min_dists[0],min_dists_values[1],min_dists[1]);				
            break;
         }
      }

      /* compute the weighted average value */
      for (close_index=0;close_index<N;close_index++)   
        if (min_dists[close_index]<INF)
        {  val+=min_dists_values[close_index]*(1/min_dists[close_index]);
           sum+=(1/min_dists[close_index]);
//printf("%d %f %f\n",close_index,min_dists_values[close_index],min_dists[close_index]);		   
        }
//printf("%d %f %f %f %f\n",N,min_dists_values[0],min_dists[0],min_dists_values[1],min_dists[1]);		
      delete min_dists;
      delete min_dists_values;
//printf("%f %f %f\n",val,sum,val/sum);	  		
      return(val/sum);
   }
//   if (method==1)
//   {  for (sample_index=0;sample_index<count;sample_index++)
//      {  if (distance(test_sample,samples[sample_index],1.0)>=0 && distance(test_sample,samples[sample_index],1.0)<min_dist_up) 
//         {  min_dist_up=distance(test_sample,samples[sample_index],1.0);
//            min_val_up=samples[sample_index]->sample_value;
//printf("val up: %f %f\n",min_val_up,min_dist_up);
//            if (closest_sample) if (min_dist_up<fabs(min_dist_down)) *closest_sample=samples[sample_index];					 
//         }
//         if (distance(test_sample,samples[sample_index],1.0)<=0 && distance(test_sample,samples[sample_index],1.0)>min_dist_down) 
//         {  min_dist_down=distance(test_sample,samples[sample_index],1.0);
//            min_val_down=samples[sample_index]->sample_value;
//            if (closest_sample) if (min_dist_down<fabs(min_dist_up)) *closest_sample=samples[sample_index];					 			
//         }
//      }
//printf("%f %f %f %f\n",min_val_down,min_val_up,min_dist_down,min_dist_up);	  
//      if (min_dist_up==INF) return(min_val_down);
//      else if (min_dist_down==INF) return(min_val_up);	  
//      else return(min_val_down+(min_val_up-min_val_down)*((-1*min_dist_down)/(min_dist_up+min_dist_down*-1)));	  
//   }   
}

/* classify3
   test_sample -signature *- a given sample to classify
   probabilities -array of double- an array (size num_classes) marginal probabilities of the given sample from each class. (ignored if NULL).
   normalization_factor -double *- the normalization factor used to compute the marginal probabilities from the distances normalization_factor=1/(dist*marginal_prob). Ignored if NULL.
*/
long TrainingSet::classify3(signatures *test_sample, double *probabilities,double *normalization_factor)
{  int dist_index,class_index,sig_index,sample_index;
   long *num_samples,*close_samples,min_samples=10000000;
   int max_class;
   double *min_dists;
   long *min_dists_classes;
   int most_probable_class=0;
   long double probs[MAX_CLASS_NUM];
   double dist;
   long size_of_class;

   /* initialize the probabilities */
   for (class_index=0;class_index<=class_num;class_index++)
     probs[class_index]=1;

   /* find the number of samples of the smallest class */
   num_samples=new long[class_num+1];
   close_samples=new long[class_num+1];
   for (class_index=0;class_index<=class_num;class_index++)
     num_samples[class_index]=0;
   for (sample_index=0;sample_index<count;sample_index++)
     num_samples[samples[sample_index]->sample_class]+=1;
   for (class_index=1;class_index<=class_num;class_index++)
     if (num_samples[class_index]<min_samples) min_samples=num_samples[class_index];

   min_dists=new double[count];
   min_dists_classes=new long[count];
   for (sig_index=0;sig_index<signature_count;sig_index++)
   {  int close_index;
      for (dist_index=0;dist_index<min_samples;dist_index++)
        min_dists[dist_index]=INF;
      for (sample_index=0;sample_index<count;sample_index++)
      {
         dist=fabs(test_sample->data[sig_index].value-samples[sample_index]->data[sig_index].value);
         /* check if this dist should be in the close list */
         for (close_index=0;close_index<count;close_index++)
         if (dist<min_dists[close_index])
         {  memmove(&(min_dists[close_index+1]),&(min_dists[close_index]),sizeof(double)*(count-1-close_index));
            memmove(&(min_dists_classes[close_index+1]),&(min_dists_classes[close_index]),sizeof(long)*(count-1-close_index));
            min_dists[close_index]=dist;
            min_dists_classes[close_index]=samples[sample_index]->sample_class;
            break;
         }
      }

      /* find the actual range of the closest sample */
      sample_index=min_samples-1;
      dist=min_dists[sample_index];
      while (sample_index<count && min_dists[sample_index]==dist)
        sample_index++;
      size_of_class=sample_index;
      if (size_of_class>=count) continue; /* no point in continuing if they all equally close */

      /* find the number of times each class appears */
      for (class_index=1;class_index<=class_num;class_index++)
        close_samples[class_index]=0;
      for (close_index=0;close_index<size_of_class;close_index++)
        close_samples[min_dists_classes[close_index]]+=1;
      /* find the max class */
      max_class=0;
      for (class_index=1;class_index<=class_num;class_index++)
        if (close_samples[class_index]>max_class) max_class=close_samples[class_index];
      /* now find the probability of each class */
      if ((double)max_class/(double)min_samples>pow(1/(double)class_num,1.0/2.0))
      for (class_index=1;class_index<=class_num;class_index++)
      {  long double class_prob;
         class_prob=((double)size_of_class/(double)(num_samples[class_index]))*(double)(close_samples[class_index])/(double)size_of_class;
//if (class_prob<1.0/(double)class_num) class_prob=1.0/(double)class_num;
         probs[class_index]=probs[class_index]*class_prob;
      }

   }

   /* normalize the results and find the most probable class */
   if (probabilities)
   {  long double sum_dists=0.0;
      long double highest_prob=0.0;
      most_probable_class=0;
      for (class_index=1;class_index<=class_num;class_index++)
        if (probs[class_index]>highest_prob)
        {  highest_prob=probs[class_index];
           most_probable_class=class_index;
        }

      for (dist_index=1;dist_index<=class_num;dist_index++)
        if (probs[dist_index]!=0)
           sum_dists+=probs[dist_index];
      for (dist_index=1;dist_index<=class_num;dist_index++)
        if (sum_dists==0 || probs[dist_index]==0) probabilities[dist_index]=0;    /* protect from division by zero */
        else probabilities[dist_index]=(probs[dist_index])/sum_dists;
     if (normalization_factor) *normalization_factor=sum_dists;				
   }

   delete num_samples;
   delete min_dists;
   delete min_dists_classes;
   delete close_samples;
   return(most_probable_class);
}

/*  Pearson
    compute pearson correlation
	This function is used by wndchrm.cpp if all class labels are numerical.
	The class labels are used as the values of one variable, and the interpolated values are used as the other
    tiles -int- the number of tiles
    avg_abs_dif -double *- the average absolute difference from the predicted and actual value
*/

double TrainingSet::pearson(int tiles, double *avg_abs_dif, double *p_value)
{  double mean=0,stddev=0,mean_ground=0,stddev_ground=0,z_score_sum=0,pearson_cor,N;
   int test_sample_index,class_index;
   if (tiles<=0) tiles=1;
   N=(double)count/(double)tiles;
   if (avg_abs_dif) *avg_abs_dif=0.0;
   /* check if the data can be interpolated (all class labels are numbers) */
   for (class_index=1;class_index<=class_num;class_index++)
     if (atof(class_labels[class_index].c_str())==0.0 && !class_labels[class_index].empty()) return(0);
   /* compute the mean */
   for (test_sample_index=0;test_sample_index<count;test_sample_index+=tiles)
   {  mean+=samples[test_sample_index]->interpolated_value;
      if (is_continuous) mean_ground+=samples[test_sample_index]->sample_value;
      else mean_ground+=atof(class_labels[samples[test_sample_index]->sample_class].c_str());
      if (avg_abs_dif) *avg_abs_dif=*avg_abs_dif+fabs(samples[test_sample_index]->sample_value-samples[test_sample_index]->interpolated_value)/N;
   }
   mean=mean/N;
   mean_ground=mean_ground/N;
   /* compute the stddev */
   for (test_sample_index=0;test_sample_index<count;test_sample_index+=tiles)
   {  stddev+=pow(samples[test_sample_index]->interpolated_value-mean,2);
      if (is_continuous) stddev_ground+=pow(samples[test_sample_index]->sample_value-mean_ground,2);
	  else stddev_ground+=pow(atof(class_labels[samples[test_sample_index]->sample_class].c_str())-mean_ground,2);
   }
   stddev=sqrt(stddev/(N-1));
   stddev_ground=sqrt(stddev_ground/(N-1));   
   /* now compute the pearson correlation */
   for (test_sample_index=0;test_sample_index<count;test_sample_index+=tiles)
     if (is_continuous) z_score_sum+=((samples[test_sample_index]->interpolated_value-mean)/stddev)*((samples[test_sample_index]->sample_value-mean_ground)/stddev_ground);
	 else z_score_sum+=((samples[test_sample_index]->interpolated_value-mean)/stddev)*((atof(class_labels[samples[test_sample_index]->sample_class].c_str())-mean_ground)/stddev_ground);
   pearson_cor=z_score_sum/(N-1);

	if (p_value) { // compute the P value of the pearson correlation
		double t=pearson_cor*(sqrt(N-2)/sqrt(1-pearson_cor*pearson_cor));
		double gamma_N1, gamma_N2;
		if ( gsl_sf_gamma (((N-2)+1)/2, &gamma_N1) == GSL_SUCCESS && gsl_sf_gamma ((N-2)/2, &gamma_N2) == GSL_SUCCESS )
			*p_value=(gamma_N1/(sqrt((N-2)*3.14159265) * gamma_N2))  *  pow((1+pow(t,2)/(N-2)),-1*(N-2+1)/2);
		else
			*p_value=0;
	}
   return(pearson_cor);
}

/* dendrogram
   generate a dendrogram 
filename -char *- a file name for   
sim_method -unsigned short- the method of transforming the similarity values into a single distance (0 - min, 1 - average. 2 - top triangle, 3 - bottom triangle).
phylip_algorithm -unsigned short- the method used by phylip
*/
long TrainingSet::dendrogram(FILE *output_file, char *dataset_name, char *phylib_path, int nodes_num,double *similarity_matrix, const std::vector<std::string> &labels, unsigned short sim_method,unsigned short phylip_algorithm)
{
	FILE *dend_file;
	int label_index,algorithm_index;
	char file_path[256],alg[16];
	sprintf(file_path,"%s/dend_file.txt",phylib_path);
	if (!(dend_file=fopen(file_path,"w"))) return(0);
	fprintf(dend_file,"%d\n",nodes_num);
/* print the labels */
	for (label_index=1;label_index<=nodes_num;label_index++) {
		char label[128];
		double dist=0.0, diff;
		int label_index2;
		strncpy(label,labels[label_index].c_str(),127);
		if (strlen(label)>8) strcpy(label,&(label[strlen(label)-8]));  /* make sure the labels are shorter or equal to 8 characters in length */
		if (!isalnum(label[strlen(label)-1])) label[strlen(label)-1]='\0';
		fprintf(dend_file,"%s                 ",label);
		for (label_index2=1;label_index2<=nodes_num;label_index2++) {
			switch (sim_method) {
			case 1: // Maximum of the two dis-similarities
				dist=max(
					1-similarity_matrix[label_index*nodes_num+label_index2],
					1-similarity_matrix[label_index2*nodes_num+label_index]
				);
			break;

			case 2:  // Average of the two dis-similarities
				dist = (
					(1-similarity_matrix[label_index*nodes_num+label_index2])
					+(1-similarity_matrix[label_index2*nodes_num+label_index])
				) / 2;
			break;

			case 3:  // top triangle
				dist =
					(1-similarity_matrix[label_index*nodes_num+label_index2]) * (label_index2>=label_index)
					+(1-similarity_matrix[label_index2*nodes_num+label_index]) * (label_index2<label_index);
			break;

			case 4:  // bottom triangle
				dist =
					(1-similarity_matrix[label_index*nodes_num+label_index2]) * (label_index2<=label_index)
					+(1-similarity_matrix[label_index2*nodes_num+label_index]) * (label_index2>label_index);
			break;

			case 6:  // Average of the two similarities
				dist=(
					similarity_matrix[label_index*nodes_num+label_index2]
					+similarity_matrix[label_index2*nodes_num+label_index]
				) /2 ;
			break;

			case 5:
			// The similarity matrix parameter is the average_class_probability matrix (a de-normalized similarity matrix).
			// The average class probabilities are used as class centroid coordinates in a "marginal probability space"
			// The distance is the euclidean distance between class centroid coordinates.
				dist = 0;
				for (int class_index = 1; class_index <= nodes_num; class_index++) {
					diff = fabs(similarity_matrix[label_index*nodes_num+class_index] - similarity_matrix[label_index2*nodes_num+class_index]);
					diff *= diff;
					dist += diff;
				}
				dist=sqrt (dist);
			break;
			} // switch
#ifndef WIN32
			if (std::isnan (dist)) dist=0;
#endif
			fprintf(dend_file,"%.4f       ",fabs(dist*(dist>=0)));
		}
		fprintf(dend_file,"\n");
	}
	fclose(dend_file);

	/* *** generate a dendrogram *** */
	sprintf(file_path,"%s/fitch.infile",phylib_path);
	/* create fith.infile */   
	if (!(dend_file=fopen(file_path,"w"))) return(0);
	fprintf(dend_file,"%s/dend_file.txt\nJ\n97\n10\nY\n",phylib_path);
	fclose(dend_file);
	/* create drawtree.infile */			
	sprintf(file_path,"%s/drawtree.infile",phylib_path);
	if (!(dend_file=fopen(file_path,"w"))) return(0);
	alg[0]='\0';
	for (algorithm_index=0;algorithm_index<phylip_algorithm;algorithm_index++)
		strcat(alg,"I\n");
	fprintf(dend_file,"outtree\n%s/exe/font1\n%sV\nN\nY\n",phylib_path,alg);     //D\n
	fclose(dend_file);
	/* create the dendrogram */   
	system("rm plotfile");
	sprintf(file_path,"%s/exe/fitch < %s/fitch.infile",phylib_path,phylib_path);
	system(file_path);
	sprintf(file_path,"%s/exe/drawtree < %s/drawtree.infile",phylib_path,phylib_path);
	system(file_path);
	sprintf(file_path,"mv plotfile ./%s.ps",dataset_name);
	system(file_path);			
	sprintf(file_path,"convert ./%s.ps ./%s.jpg",dataset_name,dataset_name);
	system(file_path);
	system("rm outfile outtree");  /* delete files from last run */			
	fprintf(output_file,"<A HREF=\"%s.ps\"><IMG SRC=\"%s.jpg\"></A><br>",dataset_name,dataset_name);
	fprintf(output_file,"<A HREF=\"%s.ps\">%s.ps</A><br>",dataset_name,dataset_name);	/* the image files are copied in the file "wndchrm.cpp" */
	return(1);
}

/*
PrintMatrix
print the confusion or similarity matrix
output_file -FILE *- the file to print into (can be stdout to print to screen)
confusion_matrix -unsigned short *- the confusion matrixvalues to print
                 NULL - don't print confusion matrix
similarity_matrix -double *- the similarity matrix values to print
                 NULL - don't print similarity matrix

returned values -long- 1 if successful, 0 if failed
*/

long TrainingSet::PrintConfusion(FILE *output_file,unsigned short *confusion_matrix, double *similarity_matrix)
{
	int class_index1,class_index2;
	
	fprintf(output_file,"%18s"," ");
	for( class_index1 = 1; class_index1 <= class_num; class_index1++ )
		fprintf( output_file, "%18s", class_labels[class_index1].c_str() );
	if( confusion_matrix ) fprintf( output_file, "%18s%20s", "Total Tested", "Per-Class Accuracy" );
	fprintf(output_file,"\n");
	
	for( class_index1 = 1; class_index1 <= class_num; class_index1++ )
	{
		int val;
		int num_class_correct = 0;
		int num_class_total = 0;
 
		fprintf( output_file, "%18s", class_labels[ class_index1 ].c_str() );
		for( class_index2 = 1; class_index2 <= class_num; class_index2++ )
		{
			if( confusion_matrix ) {
				val = confusion_matrix[class_index1*class_num+class_index2];
				fprintf( output_file, "%18d", val);
				if( class_index1 == class_index2 )
					num_class_correct = val;
				num_class_total += val;
			}
			else
				fprintf(output_file,"%11s%1.5f"," ",similarity_matrix[class_index1*class_num+class_index2]);
		}
		if( confusion_matrix ) fprintf( output_file, "%18d%13s%1.5f", num_class_total, " ", double(num_class_correct)/double(num_class_total) );

		fprintf(output_file,"\n");
	}

	// Write out the result of classifying unknown samples.
	for (class_index2=1;class_index2<=class_num;class_index2++) {
		if (confusion_matrix && confusion_matrix[0+class_index2] > 0) break;
		else if (similarity_matrix && similarity_matrix[0+class_index2] > 0) break;
	}
	if (class_index2 <= class_num)
	{
		fprintf(output_file,"%18s","UNKNOWN");
		class_index1=0;
		for (class_index2=1;class_index2<=class_num;class_index2++)
		{
			if (confusion_matrix)
				fprintf(output_file,"%18d",confusion_matrix[class_index1*class_num+class_index2]);
			else
				fprintf(output_file,"%11s%1.5f"," ",similarity_matrix[class_index1*class_num+class_index2]);
		}
		fprintf(output_file,"\n");
	}
	fprintf(output_file,"\n");
	return(1);
}

/*
   Returns 0 if the string in *s cannot be interpreted numerically.
   Returns 1 if the string can be interpreted numerically, but contains additional characters
   Returns 2 if all the characters in *s are part of a valid number.
*/
int check_numeric (const char *s, double *samp_val) {
char *p2;
int numeric=1;
int pure_numeric=1;
long maybe_int;
double val;
int last_errno;

	// Checking errno is the only way we have to know that the numeric conversion had an error
	last_errno = errno;

	// Check for float
	errno = 0;
	val = strtod(s, &p2);
	if (errno) {
		numeric = 0; pure_numeric = 0;
	} else {
		numeric = 1; pure_numeric = 1;
		if (p2 == s || *p2 != '\0') pure_numeric = 0;
	}

	// Weird ints, hex, etc. Later C standards will interpret hex as float
	if (!pure_numeric || !numeric) {
		errno = 0;
		maybe_int = strtol(s, &p2, 0);
		if (errno) {
			numeric = 0; pure_numeric = 0;
		} else {
			numeric = 1; pure_numeric = 1;
			val = (double) maybe_int;
			if (p2 == s || *p2 != '\0') pure_numeric = 0;
		}
	}
	
	// Lastly, val has to be in a valid double range
	// We're making the range padded with DBL_EPSILON on the low and high end.
	if ( ! (val > (-DBL_MAX+DBL_EPSILON) && val < (DBL_MAX-DBL_EPSILON)) ) { numeric = 0; pure_numeric = 0; }

	if (numeric && samp_val) *samp_val = val;
	errno = last_errno;
	return (numeric+pure_numeric);
}


void chomp (char *line) {
	size_t len;
	char *char_p;
	if (!line || !*line) return;
	len = strlen ( line );
	char_p = line+len-1;
	while (char_p >= line && (*char_p == '\n' || *char_p == '\r')) *char_p-- = '\0';
}

long TrainingSet::report(FILE *output_file, int argc, char **argv, char *output_file_name, data_split *splits, unsigned short split_num, featureset_t *featureset, int max_train_images,char *phylib_path, int distance_method, int phylip_algorithm, int export_tsv, TrainingSet *testset,int image_similarities)
{
	int class_index,class_index2,split_index,test_set_size,train_set_size;
	double *avg_similarity_matrix,*avg_class_prob_matrix;
	double splits_accuracy,splits_class_accuracy,avg_pearson=0.0,avg_abs_dif=0.0,avg_p=0.0;
	FILE *tsvfile;
	char buffer[512];
	char bgcolor[64];
  
	int skip_split_reporting = 0;

   /* create a directory for the files */
#ifndef WIN32
	if (export_tsv) mkdir("tsv",0755);
#else
	if (export_tsv) mkdir("tsv");
#endif

	time_t timeval = time(NULL);
	
	strftime (buffer, 512, "%Y-%m-%d %H:%M:%S %Z", localtime (&timeval));
  // print the header
	fprintf(output_file,"<HTML>\n<HEAD>\n<TITLE> %s </TITLE>\n </HEAD> \n <BODY> \n <br> WNDCHRM "PACKAGE_VERSION".&nbsp;&nbsp;&nbsp;%s\n <br><br> <h1>%s</h1>\n ",
		output_file_name,buffer,this->name);
	// print the training set summary
	fprintf(output_file,"<table id=\"trainset_summary\" border=\"1\" cellspacing=\"0\" cellpadding=\"3\" > \n");
	fprintf(output_file,"<caption>%ld Images.",(long)(count/featureset->n_samples));
	if (featureset->n_samples > 1) fprintf(output_file," Samples per image: %d, total samples: %ld.",featureset->n_samples, count);
	fprintf(output_file,"</caption> \n <tr>");
	fprintf(output_file,"<tr><th>Class</th>");
	if (is_numeric) fprintf(output_file,"<th>Value</th>");
	fprintf(output_file,"<th>Images");
	if (featureset->n_samples > 1) fprintf(output_file," (Samples)");
	fprintf(output_file,"</th></tr>");
	for (class_index=1;class_index<=class_num;class_index++) {
		fprintf(output_file,"<tr><th>%s</th>\n",class_labels[class_index].c_str());
		if (is_numeric) fprintf(output_file,"<td>%.3g</td>",atof(class_labels[class_index].c_str()));
		fprintf(output_file,"<td>%ld",class_nsamples[class_index]/featureset->n_samples);
		if (featureset->n_samples > 1) fprintf(output_file," (%ld)", class_nsamples[class_index]);
		fprintf(output_file,"</td></tr>");
	}
	// print the unknown class
	if (class_nsamples[0]) {
		fprintf(output_file,"<tr><th>UNKNOWN</th>");
		if (is_numeric) fprintf(output_file,"<td></td>");
		fprintf(output_file,"<td>%ld",class_nsamples[0]/featureset->n_samples);
		if (featureset->n_samples > 1) fprintf(output_file," (%ld)", class_nsamples[0]);
		fprintf(output_file,"</td></tr>");
	}
	fprintf(output_file,"</table>\n");

	// print the test set summary
	if (testset) {
		fprintf(output_file,"<br><br><br>\n");
		fprintf(output_file,"<h3>Testing with data file:<br>%s</h3>",testset->source_path);
		fprintf(output_file,"<table id=\"testset_summary\" border=\"1\" cellspacing=\"0\" cellpadding=\"3\" > \n");
		fprintf(output_file,"<caption>%ld Images.",(long)(testset->count/featureset->n_samples));
		if (featureset->n_samples > 1) fprintf(output_file," Samples per image: %d, total samples: %ld.",featureset->n_samples, testset->count);
		fprintf(output_file,"</caption> \n <tr>");
		fprintf(output_file,"<tr><th>Class</th>");
		if (testset->is_numeric) fprintf(output_file,"<th>Value</th>");
		fprintf(output_file,"<th>Images");
		if (featureset->n_samples > 1) fprintf(output_file," (Samples)");
		fprintf(output_file,"</th></tr>");
		for (class_index=1;class_index<=testset->class_num;class_index++) {
			fprintf(output_file,"<tr><th>%s</th>\n",testset->class_labels[class_index].c_str());
			if (is_numeric) fprintf(output_file,"<td>%.3g</td>",atof(testset->class_labels[class_index].c_str()));
			fprintf(output_file,"<td>%ld",testset->class_nsamples[class_index]/featureset->n_samples);
			if (featureset->n_samples > 1) fprintf(output_file," (%ld)", testset->class_nsamples[class_index]);
			fprintf(output_file,"</td></tr>");
		}
		// print the unknown class
		if (testset->class_nsamples[0]) {
			fprintf(output_file,"<tr><th>UNKNOWN</th>");
			if (testset->is_numeric) fprintf(output_file,"<td></td>");
			fprintf(output_file,"<td>%ld",testset->class_nsamples[0]/featureset->n_samples);
			if (featureset->n_samples > 1) fprintf(output_file," (%ld)", testset->class_nsamples[0]);
			fprintf(output_file,"</td></tr>");
		}
		fprintf(output_file,"</table>\n");
	}
	
	// print the command line
	if (argc && argv && *argv) {
		int i;
		fprintf(output_file,"<br><br>Command line: <pre>");
		for (i = 0; i < argc; i++)
			fprintf(output_file," %s",argv[i]);
		fprintf(output_file,"</pre><br>");
	}
	
	// print the warnings
	std::string html_errors = getErrorString();

	if (html_errors.size()) {
		fprintf(output_file,"<font color=\"#FF0000\">Warnings:<pre>");
		fprintf(output_file,"%s",html_errors.c_str());
		fprintf(output_file,"</font></pre><br>");
	}

	fprintf(output_file,"<hr/><CENTER>\n");
   
  // print the number of samples table
	fprintf(output_file,"<table id=\"classifier_split_params\" border=\"1\" cellspacing=\"0\" cellpadding=\"3\" align=\"center\"> \n <caption>Images for training and testing");
	if (split_num > 1) fprintf(output_file," (per-split)");
	fprintf(output_file,"</caption> \n <tr>");
	for (class_index=0;class_index<=class_num;class_index++)
		fprintf(output_file,"<th>%s</th>\n",class_labels[class_index].c_str());
	fprintf(output_file,"<th>total</th></tr>\n");
	test_set_size=0;
	fprintf(output_file,"<tr><th>Testing</th>\n");

	if (is_continuous) test_set_size=splits[0].confusion_matrix[0];
	else
		for (class_index=1;class_index<=class_num;class_index++) {
			fprintf(output_file,"<td>%d</td>\n",splits[0].testing_images[class_index]);
			test_set_size += splits[0].testing_images[class_index];
		}
	fprintf(output_file,"<td>%d</td></tr>\n",test_set_size); /* add the total number of test samples */
	train_set_size=0;
	fprintf(output_file,"<tr>\n<th>Training</th>\n");
	if (is_continuous) {  train_set_size=count/(featureset->n_samples)-test_set_size;
		if (max_train_images!=0 && max_train_images<train_set_size) train_set_size=max_train_images;
	}
	for (class_index=1;class_index<=class_num;class_index++) {
		fprintf(output_file,"<td>%d</td>\n",splits[0].training_images[class_index]);
		train_set_size += splits[0].training_images[class_index];
	}

	fprintf(output_file,"<td>%d</td>\n",train_set_size); /* add the total number of training samples */
	fprintf(output_file,"</tr> \n </table><br>\n");          /* close the number of samples table */

	if( split_num > 100 ) {
		skip_split_reporting = 1;
		fprintf(output_file,"<br>(Skipping individual split reporting since # Splits > 100)<br>\n");
	}

	/* print the splits */
	splits_accuracy=0.0;
	splits_class_accuracy=0.0;
	int total_tested=0, total_correct=0;
	fprintf(output_file,"<h2>Results</h2> \n <table id=\"test_results\" border=\"1\" align=\"center\"><caption></caption> \n");
	for (split_index=0;split_index<split_num;split_index++) {
		unsigned short *confusion_matrix;
		double *similarity_matrix,*class_probability_matrix;

		data_split *split = &(splits[split_index]);
		confusion_matrix = split->confusion_matrix;
		similarity_matrix = split->similarity_matrix;
		class_probability_matrix = split->class_probability_matrix;

		total_tested += split->known_images;
		total_correct += split->accurate_predictions;

		if( !skip_split_reporting ) 
		{
			fprintf(output_file,"<tr> <td>Split %d</td> \n <td align=\"center\" valign=\"top\"> \n",split_index+1);
			if (class_num>0) {
				fprintf(output_file,"Accuracy: <b>%.2f of total (P=%.3g) </b><br> \n",split->accuracy,split->classification_p_value);	  
				fprintf(output_file,"<b>%.2f &plusmn; %.2f Avg per Class Correct of total</b><br> \n",split->avg_class_accuracies,split->plus_minus);
			}

			if (split->pearson_coefficient!=0) {
				avg_pearson+=split->pearson_coefficient;
				avg_abs_dif+=split->avg_abs_dif; 
				avg_p+=split->pearson_p_value;
			}

			if (split->pearson_coefficient!=0) {
				fprintf(output_file,"Pearson correlation coefficient: %.2f (P=%.3g) <br>\n",split->pearson_coefficient,split->pearson_p_value);
				fprintf(output_file,"Mean absolute difference: %.4f <br>\n",split->avg_abs_dif);
			}

			if (split->feature_weight_distance>=0)
				fprintf(output_file,"Feature weight distance: %.2f<br>\n",split->feature_weight_distance);	  	  
			fprintf(output_file,"<a href=\"#split%d\">Full details</a><br> </td></tr>\n",split_index);
			// fprintf(output_file,"<a href=\"#features%d\">Features used</a><br> </td> </tr> \n",split_index);
		}
		splits_accuracy+=split->accuracy;
		splits_class_accuracy+=split->avg_class_accuracies;
	}

	double accuracy;
	double std_error_of_mean;
	// Using normal approximation of binomial distribution to calculate standard error of the mean
	// For more info, see http://en.wikipedia.org/wiki/Binomial_proportion_confidence_interval
	double confidence_interval;
	// The confidence interval is S.E.M. * quantile for your chosen accuracy
	// The quantile for 95% accuracy is ~ 1.96.
	double z_score = 1.95996;
	double wilson_score_error_bar;
	double wilson_interval_center;
	double n;
	bool use_wilson = false;

	// average of all splits
	fprintf(output_file,"<tr> <td>Total</td> \n <td id=\"overall_test_results\" align=\"center\" valign=\"top\"> \n");
	if (class_num>0) {
		double avg_p2=0.0;
		double choose;
		for( int correct = total_correct; correct <= total_tested; correct++ ) {
			// gsl_sf_choose (n,m) = n!/(m!(n-m)!)
			if( gsl_sf_choose( total_tested, correct, &choose ) == GSL_SUCCESS ) {
				avg_p2 += std::pow( (1/(double) class_num), correct) * pow( 1 - 1/(double)class_num, total_tested - correct ) * choose;
			}
		}
//printf("%i %i %f %i %i %f %f\n",class_num,count,splits_accuracy/split_num,(long)(count/featureset->n_samples),(long)((long)(count/featureset->n_samples)*(splits_accuracy/split_num)),0.0,avg_p2);
		if( skip_split_reporting ) {
			fprintf( output_file, "Number of splits: %i<br>", split_num );
		}
		fprintf(output_file,"Total tested: %d<br> \n", total_tested);
		fprintf(output_file,"Total correct: %d<br> \n", total_correct);

		//fprintf(output_file,"%.3f Avg per Class Correct of total<br> \n", splits_class_accuracy / split_num);
		fprintf(output_file,"Accuracy: <b>%0.1f%% of total (P=%.3g)</b><br> \n", splits_accuracy / split_num * 100, avg_p2);

		n = total_tested;
		accuracy = double(total_correct) / n;

		if( (n * accuracy) > 5 && (n * (1- accuracy)) > 5 ) {
			//use normal approximation ofbinomial distribution
			std_error_of_mean = sqrt( accuracy * (1-accuracy) / n );
			confidence_interval = z_score * std_error_of_mean;
			fprintf(output_file,"Classification accuracy: %0.1f +/- %0.1f%% (95%% confidence, normal approx confidence interval)<br> \n", accuracy*100, confidence_interval*100 );
		}
		else
		{
			use_wilson = true;
			wilson_score_error_bar = z_score * sqrt( accuracy * (1-accuracy) / n + z_score * z_score / (4 * n * n) ) / ( 1 + z_score * z_score / n );
			wilson_interval_center = (accuracy + z_score * z_score / (2 * n) ) / ( 1 + z_score * z_score / n );
			fprintf(output_file,"Classification accuracy: %0.1f +/- %0.1f%% (95%% confidence, wilson score confidence interval)<br> \n", wilson_interval_center*100, wilson_score_error_bar*100 );
		}	
	}
	if (avg_pearson!=0) {
		fprintf(output_file,"Pearson correlation coefficient: %.2f (avg P=%.3g) <br>\n", avg_pearson / split_num, avg_p / split_num);
		fprintf(output_file,"Mean absolute difference: %.4f <br>\n", avg_abs_dif / split_num);
	}   

	fprintf(output_file,"</table>\n");   /* close the splits table */

	fprintf(output_file,"<br><br><br><br> \n\n\n\n\n\n\n\n");

	// average (sum) confusion matrix
	sprintf(buffer,"tsv/avg_confusion.tsv");
	tsvfile=NULL;
	if (export_tsv) tsvfile=fopen(buffer,"w");
	if (class_num>0) fprintf(output_file,"<table id=\"master_confusion_matrix\" border=\"1\" align=\"center\"><caption>Confusion Matrix (sum of all splits)</caption> \n <tr><td></td> ");
	if (tsvfile) fprintf(tsvfile,"\t");         // space (in the tsv file)
	for (class_index=1;class_index<=class_num;class_index++)
	{  
		fprintf(output_file,"<th>%s</th> ",class_labels[class_index].c_str()); // print to the html file
		if (tsvfile) fprintf(tsvfile,"%s\t",class_labels[class_index].c_str()); // print into the tsv file
	}
  fprintf( output_file, "<th></th><th>Total Tested</th><th>Per-Class Accuracy</th></tr>\n" );
	if (tsvfile) fprintf(tsvfile,"\n");     // end of the classes names in the tsv file

	 double observation;
//	 std::vector< std::vector<double> > observations ( class_num * class_num );

	 for( int row = 1; row <= class_num; row++ )
	 { 
		 // print the class names as the column headers
		 fprintf( output_file,"<tr><th>%s</th> ", class_labels[ row ].c_str() );
		 if (tsvfile) fprintf( tsvfile, "%s\t", class_labels[ row ].c_str() );

		 int num_class_correct = 0;
		 int num_class_total = 0;
		 for( int col = 1; col <= class_num; col++ )
		 {
			 double sum = 0.0;
			 for( split_index = 0; split_index < split_num; split_index++ )
			 {
				 observation = splits[ split_index ].confusion_matrix[ row * class_num + col ];
				 sum += observation;
//				 observations[ (row-1) * class_num + (col-1) ].push_back(observation);
			 }
			 num_class_total += int(sum);
			 if( row == col ) {
				 strcpy( bgcolor," bgcolor=#D5D5D5" );
				 num_class_correct = int(sum);
			 }
			 else strcpy(bgcolor,"");  
			 if ((double)((long)(sum/split_num))==sum/split_num) fprintf(output_file,"<td%s>%ld</td>\n",bgcolor,(long)(sum/*/split_num*/));
			 else fprintf(output_file,"<td%s>%.0f</td> ",bgcolor,sum/*/split_num*/);
			 if (tsvfile) fprintf(tsvfile,"%.0f\t",sum/*/split_num*/);     /* print the values to the tsv file (for the tsv machine readable file a %.2f for all values should be ok) */		 
		 }
		 n = num_class_total;
		 accuracy = double(num_class_correct) / n;
		 if( !use_wilson )
		 {
			 std_error_of_mean = sqrt( accuracy * (1-accuracy) / n );
			 confidence_interval = z_score * std_error_of_mean;
			 fprintf(output_file,"<td></td><td>%i</td><td>%0.1f +/- %0.1f%%</td></tr>\n", num_class_total, accuracy*100, confidence_interval*100 );
		 }
		 else
		 {
			 wilson_score_error_bar = z_score * sqrt( accuracy * (1-accuracy) / n + z_score * z_score / (4 * n * n) ) / ( 1 + z_score * z_score / n );
			 wilson_interval_center = (accuracy + z_score * z_score / (2 * n) ) / ( 1 + z_score * z_score / n );
			 fprintf(output_file,"<td></td><td>%i</td><td>%0.1f +/- %0.1f%%</td></tr>\n", num_class_total, wilson_interval_center*100, wilson_score_error_bar*100 );
		 }
		 if (tsvfile) fprintf(tsvfile,"\n");
	 }
	 fprintf(output_file,"</table>\nIntervals based on 95%% confidence using %s method.<br><br> \n", use_wilson ? "Wilson Score" : "Normal Approximation");  // end of average confusion matrix
	 if (tsvfile) fclose(tsvfile);

#if 0
	 std::ofstream confusion_outcomes ( "confusion.csv", std::ios::trunc );

	 for( int ii = 0 ; ii < split_num; ii++ ) {
		 for( int jj = 0; jj < class_num*class_num; jj++) {
			 confusion_outcomes << observations[jj][ii] << ",";
		 }
		 confusion_outcomes << std::endl;
	 }

	confusion_outcomes.close();

	std::ofstream variance_study ( "variances.csv", std::ios::trunc );

	std::vector<double>::iterator var_it;

	double subtotal;
	int count;
	for( int kk = 1; kk <= 100; kk++ ) {
		count = 1;
		subtotal = 0;
		for( var_it = observations[0].begin(); var_it != observations[0].end(); ++var_it ) {
			subtotal += *var_it;
			if( (count++ % kk) == 0 ) {
				variance_study << subtotal/kk << ",";
				subtotal = 0;
			}
		}
		variance_study << std::endl;
	}

	variance_study.close();
#endif

  // average similarity matrix - also used for creating the dendrograms 
	sprintf(buffer,"tsv/avg_similarity.tsv");
	tsvfile = NULL;
	if (export_tsv) tsvfile=fopen(buffer,"w");

	avg_similarity_matrix=new double[(class_num+1)*(class_num+1)];	
	if (class_num>0) fprintf(output_file,"<table id=\"average_similarity_matrix\" border=\"1\" align=\"center\"><caption>Average Similarity Matrix</caption>\n <tr><td></td> ");
	if (tsvfile) fprintf(tsvfile,"\t");
	for (class_index=1;class_index<=class_num;class_index++)
	{
		fprintf(output_file,"<th>%s</th> ",class_labels[class_index].c_str());
		if (tsvfile) fprintf(tsvfile,"%s\t",class_labels[class_index].c_str());
	}
	fprintf(output_file,"</tr>\n");
	if (tsvfile) fprintf(tsvfile,"\n");

	for( class_index = 1; class_index <= class_num; class_index++ )
	{
		fprintf(output_file,"<tr><th>%s</th> ",class_labels[class_index].c_str());
		if (tsvfile) fprintf(tsvfile,"%s\t",class_labels[class_index].c_str());         /* print the class name into the tsv file */
		for (class_index2=1;class_index2<=class_num;class_index2++)
		{
			double sum=0.0;
			for( split_index = 0; split_index < split_num; split_index++ )
				sum+=splits[split_index].similarity_matrix[class_index*class_num+class_index2];
			avg_similarity_matrix[class_index*class_num+class_index2]=sum/split_num;    /* remember this value for the dendrogram file */
			if (class_index==class_index2) strcpy(bgcolor," bgcolor=#D5D5D5");
			else strcpy(bgcolor,"");  
			fprintf(output_file,"<td%s>%.2f</td> ",bgcolor,sum/split_num);
			if (tsvfile) fprintf(tsvfile,"%.2f\t",sum/split_num);              /* print the values to the tsv file (for the tsv machine readable file a %.2f for all values should be ok) */		 		 
		}
		fprintf(output_file,"</tr>\n");                         /* end of the line in the html report   */
		if (tsvfile) fprintf(tsvfile,"\n");                     /* end of the line in the tsv file      */	  
	}
	fprintf(output_file,"</table><br>");   /* end of average similarity matrix */
	if (tsvfile) fclose(tsvfile);

   /* average class probability matrix */
	sprintf(buffer,"tsv/avg_class_prob.tsv");                 /* determine the tsv file name               */
	tsvfile=NULL;                                                   /* keep it null if the file doesn't open     */
	if (export_tsv) tsvfile=fopen(buffer,"w");                /* open the file for tsv                     */   
	avg_class_prob_matrix=new double[(class_num+1)*(class_num+1)];  /* this is used for creating the dendrograms */
	if (class_num>0) fprintf(output_file,"<table id=\"average_class_probability_matrix\" border=\"1\" align=\"center\"><caption>Average Class Probability Matrix</caption>\n <tr><td></td> ");
	if (tsvfile) fprintf(tsvfile,"\t");         /* space */   
	for (class_index=1;class_index<=class_num;class_index++) {
		fprintf(output_file,"<th>%s</th> ",class_labels[class_index].c_str());   /* print to the html file  */
		if (tsvfile) fprintf(tsvfile,"%s\t",class_labels[class_index].c_str());         /* print into the tsv file */
	}
	fprintf(output_file,"</tr>\n");         /* end of the classes names */
	if (tsvfile) fprintf(tsvfile,"\n");     /* end of the classes names in the tsv file */

	for (class_index=1;class_index<=class_num;class_index++) {
		fprintf(output_file,"<tr><th>%s</th> ",class_labels[class_index].c_str());
		if (tsvfile) fprintf(tsvfile,"%s\t",class_labels[class_index].c_str());         /* print the class name into the tsv file */
		for (class_index2=1;class_index2<=class_num;class_index2++) {
			double sum=0.0;
			for (split_index=0;split_index<split_num;split_index++)
				sum+=splits[split_index].class_probability_matrix[class_index*class_num+class_index2];
			avg_class_prob_matrix[class_index*class_num+class_index2]=sum/split_num;    /* remember this value for the dendrogram file */
			if (class_index==class_index2) strcpy(bgcolor," bgcolor=#D5D5D5");
			else strcpy(bgcolor,"");  
			fprintf(output_file,"<td%s>%.2f</td> ",bgcolor,sum/split_num);
			if (tsvfile) fprintf(tsvfile,"%.2f\t",sum/split_num);              /* print the values to the tsv file (for the tsv machine readable file a %.2f for all values should be ok) */		 		 
		}
		fprintf(output_file,"</tr>\n");                         /* end of the line in the html report   */
		if (tsvfile) fprintf(tsvfile,"\n");                     /* end of the line in the tsv file      */	  
	}
	fprintf(output_file,"</table>");   /* end of average class probability matrix */
	if (tsvfile) fclose(tsvfile);

	// report statistics on features across splits
	if( aggregated_feature_stats ) {
		int features_num = aggregated_feature_stats->size();
		fprintf(output_file,"<br>Top 50 image features across splits:<br> ");
		fprintf(output_file,"<TABLE ID=\"aggregated_feature_stats\" border=\"1\" >\n");
		fprintf(output_file,"<tr><th>Rank</th><th>Name</th><th>Min</th><th>Max</th><th>Mean</th><th>Std. dev.</th></tr>\n");
		featuregroup_stats_t *featuregroups_stats;
		for( int tr = 0; tr < features_num; tr++ ) {
			featuregroups_stats = &( (*aggregated_feature_stats)[tr] );
			fprintf(output_file,"<tr><td>%d</td><td>%s</td><td>%.4g</td><td>%.4g</td><td>%.4g</td><td>%.4g</td></tr>\n",
				tr+1, featuregroups_stats->name.c_str(), featuregroups_stats->min, featuregroups_stats->max,
				featuregroups_stats->mean, featuregroups_stats->stddev);
		}
		fprintf(output_file,"</table><br>\n");
	}

	/* *** generate a dendrogram *** */
	if (phylib_path && class_num>0 ) {  /* generate a dendrogram only if phlyb path was specified */
		if (distance_method == 5)
			dendrogram(output_file,this->name, phylib_path, class_num,avg_class_prob_matrix, class_labels,distance_method,phylip_algorithm);
		else
			dendrogram(output_file,this->name, phylib_path, class_num,avg_similarity_matrix, class_labels,distance_method,phylip_algorithm);
		if (export_tsv) {  /* write the phylip file to the tsv directory */
			sprintf(buffer,"cp %s/dend_file tsv/dend_file.txt",phylib_path);
			system(buffer);
		}   
	}

	fprintf(output_file,"<br><br><br><br> \n");
// deallocate averaging matrixes
	delete avg_similarity_matrix;
	delete avg_class_prob_matrix;

   
//      FILE *dend_file;
//      char file_path[256],alg[16];
//	  int algorithm_index;	  
//      /* write "dend_file.txt" */
//      sprintf(file_path,"%s/dend_file.txt",phylib_path);
//      dend_file=fopen(file_path,"w");
//      if (dend_file)
//	  {  PrintConfusion(dend_file,splits[0].confusion_matrix,avg_similarity_matrix,1,1);  /* print the dendrogram to a the "dend_file.txt" file */
//         fclose(dend_file);
//		 if (export_tsv)   /* write the phylip file to the tsv directory */
//		 {  sprintf(file_path,"tsv/dend_file.txt");
//		    dend_file=fopen(file_path,"w");
//			PrintConfusion(dend_file, splits[0].confusion_matrix,avg_similarity_matrix,1,1);  /* print the dendrogram to a "dend_file.txt" file */
//			fclose(dend_file);
//		 }
//         sprintf(file_path,"%s/fitch.infile",phylib_path);
//         dend_file=fopen(file_path,"w");
//         if (dend_file)
//         {  /* create fith.infile */
//            fprintf(dend_file,"%s/dend_file.txt\nJ\n97\n10\nY\n",phylib_path);
//            fclose(dend_file);
//            /* create drawtree.infile */			
//            sprintf(file_path,"%s/drawtree.infile",phylib_path);
//            dend_file=fopen(file_path,"w");
//			alg[0]='\0';
//			for (algorithm_index=0;algorithm_index<phylip_algorithm;algorithm_index++)
//			  strcat(alg,"I\n");
//            fprintf(dend_file,"outtree\n%s/exe/font1\n%sV\nN\nY\n",phylib_path,alg);     //D\n
//            fclose(dend_file);
//			/* create the dendrogram */
//			system("rm plotfile");
//            sprintf(file_path,"%s/exe/fitch < %s/fitch.infile",phylib_path,phylib_path);
//            system(file_path);
//            sprintf(file_path,"%s/exe/drawtree < %s/drawtree.infile",phylib_path,phylib_path);
//            system(file_path);
//            sprintf(file_path,"mv plotfile ./%s.ps",name);
//            system(file_path);			
//            sprintf(file_path,"convert ./%s.ps ./%s.jpg",name,name);
//            system(file_path);
//            system("rm outfile outtree");  /* delete files from last run */			
//            fprintf(output_file,"<A HREF=\"%s.ps\"><IMG SRC=\"%s.jpg\"></A><br>",name,name);
//            fprintf(output_file,"<A HREF=\"%s.ps\">%s.ps</A><br>",name,name);			
//         }
//	}		   
//   }

	// print the average accuracies of the tile areas
	if (splits[0].tile_area_accuracy)
	{  
		fprintf(output_file,"<br><table id=\"tile_area_accuracy\" border=\"1\" align=\"center\"><caption>Tile Areas Accuracy</caption> \n");
		for (int y=0;y<featureset->sampling_opts.tiles_y;y++) 
		{   
			fprintf(output_file,"<tr>\n");
			for (int x=0;x<featureset->sampling_opts.tiles_x;x++)
			{  
				splits_accuracy=0.0;
				for (split_index=0;split_index<split_num;split_index++)
					splits_accuracy+=splits[0].tile_area_accuracy[y*featureset->sampling_opts.tiles_x+x];
				fprintf(output_file,"<td>%.3f</td>\n",splits_accuracy/(double)split_num);
			}
			fprintf(output_file,"</tr>\n");
		}
		fprintf(output_file,"</table><br>\n");
	}

	if( skip_split_reporting ) {
		fprintf(output_file,"</CENTER> \n </BODY> \n </HTML>\n");
		return (1);
	}

	// print the confusion/similarity matrices, feature names and individual images for the splits
	for (split_index=0;split_index<split_num;split_index++) {
		unsigned short *confusion_matrix;
		double *similarity_matrix, *class_probability_matrix;
		unsigned short features_num=0,class_index;

		data_split *split = &(splits[split_index]);
		confusion_matrix = split->confusion_matrix;
		similarity_matrix = split->similarity_matrix;
		class_probability_matrix = split->class_probability_matrix;

		fprintf(output_file,"<HR><BR><A NAME=\"split%d\">\n",split_index);   /* for the link to the split */
		fprintf(output_file,"<B>Split %d</B><br><br>\n",split_index+1);

		if (class_num>0) {
			// print the confusion matrix
			fprintf(output_file,"<table  id=\"confusion_matrix-split%d\" border=\"1\" align=\"center\"><caption>Confusion Matrix</caption> \n", split_index);

			fprintf (output_file, "<tr><th></th>\n");
			for (class_index=1;class_index<=class_num;class_index++)
				fprintf(output_file,"<th>%s</th>\n",class_labels[class_index].c_str());
			fprintf(output_file,"</tr>\n");

			for (class_index=1;class_index<=class_num;class_index++)
			{
				fprintf(output_file,"<tr><th>%s</th>\n",class_labels[class_index].c_str());
				for (class_index2=1;class_index2<=class_num;class_index2++)
				{
					if (class_index==class_index2) strcpy(bgcolor," bgcolor=#D5D5D5");
					else strcpy(bgcolor,"");  
					fprintf(output_file,"<td%s>%d</td>\n",bgcolor,confusion_matrix[class_index*class_num+class_index2]);
				}
				fprintf(output_file,"</tr>\n");
			}
			fprintf(output_file,"</table> \n <br><br> \n");


			// print the similarity matrix
			fprintf (output_file, "<table id=\"similarity_matrix-split%d\" border=\"1\" align=\"center\"><caption>Similarity Matrix</caption> \n", split_index);

			fprintf (output_file, "<tr><th></th>\n");
			for (class_index=1;class_index<=class_num;class_index++)
				fprintf(output_file,"<th>%s</th>\n",class_labels[class_index].c_str());   
			fprintf(output_file,"</tr>\n");

			for (class_index=1;class_index<=class_num;class_index++)
			{
				fprintf(output_file,"<tr><th>%s</th>\n",class_labels[class_index].c_str());
				for (class_index2=1;class_index2<=class_num;class_index2++)
				{
					if (class_index==class_index2) strcpy(bgcolor," bgcolor=#D5D5D5");
					else strcpy(bgcolor,"");  
					fprintf(output_file,"<td%s>%.2f</td>\n",bgcolor,similarity_matrix[class_index*class_num+class_index2]);
				}
				fprintf(output_file,"</tr>\n");
			}
			fprintf(output_file,"</table><br>\n");

			// print the average class probabilities
			fprintf (output_file, "<table id=\"class_probability_matrix-split%d\" border=\"1\" align=\"center\"><caption>Class Probability Matrix</caption> \n", split_index);
			fprintf (output_file, "<tr><th></th>\n");
			for (class_index=1;class_index<=class_num;class_index++)
				fprintf(output_file,"<th>%s</th>\n",class_labels[class_index].c_str());   
			fprintf(output_file,"</tr>\n");

			for (class_index=1;class_index<=class_num;class_index++)
			{
				fprintf(output_file,"<tr><th>%s</th>\n",class_labels[class_index].c_str());
				for (class_index2=1;class_index2<=class_num;class_index2++)
				{
					if (class_index==class_index2) strcpy(bgcolor," bgcolor=#D5D5D5");
					else strcpy(bgcolor,"");  
					fprintf(output_file,"<td%s>%.2f</td>\n",bgcolor,class_probability_matrix[class_index*class_num+class_index2]);
				}
				fprintf(output_file,"</tr>\n");
			}
			fprintf(output_file,"</table>\n");

		}

		/* add a dendrogram of the image similarities */
		if (image_similarities && split->image_similarities)
		{  
			char file_name[256];
			int test_image_index;
			sprintf(file_name,"%s_%d",name,split_index);
			std::vector<std::string> labels;
			for (test_image_index=1;test_image_index<=test_set_size;test_image_index++)
				labels.push_back (class_labels[(int)(split->image_similarities[test_image_index])].c_str());
			dendrogram(output_file,file_name, phylib_path, test_set_size,(double *)(split->image_similarities), labels,6,phylip_algorithm);	    
		}

		/* add the sorted features */
		features_num = split->feature_stats.size();
		if (features_num > 0) fprintf(output_file,"<br>%d features selected (out of %ld features computed).<br> "
				"<a href=\"#\" onClick=\"sigs_used=document.getElementById('FeaturesUsed_split%d'); "
				"if (sigs_used.style.display=='none'){ sigs_used.style.display='inline'; } else { sigs_used.style.display='none'; } return false;"
				"\">Toggle feature names</a><br><br>\n",features_num,signature_count,split_index);
		fprintf(output_file,"<TABLE ID=\"FeaturesUsed_split%d\" border=\"1\" style=\"display: none;\">\n",split_index);
		fprintf(output_file,"<tr><th>Rank</th><th>Name</th><th>Weight</th></tr>\n");
		for (int tr=0; tr < features_num; tr++)
		{
			fprintf(output_file,"<tr><td>%d</td><td>%s</td><td>%.4g</td></tr>\n",tr+1,
					split->feature_stats[tr].name.c_str(),split->feature_stats[tr].weight);
		}
		fprintf(output_file,"</table><br>\n"); 

		/* add the feature groups */
		features_num = split->featuregroups_stats.size();
		if( features_num > 0 ) fprintf( output_file, "<a href=\"#\" onClick=\"sigs_used=document.getElementById('FeaturesGroups_split%d'); "
				"if (sigs_used.style.display=='none'){ sigs_used.style.display='inline'; } else { sigs_used.style.display='none'; } return false; "
				"\">Analysis of Fisher scores for each feature family, ranked by mean Fisher score</a><br><br>\n",split_index);
		fprintf(output_file,"<TABLE ID=\"FeaturesGroups_split%d\" border=\"1\" style=\"display: none;\">\n",split_index);
		fprintf(output_file,"<tr><th>Rank</th><th>Name</th><th>Min</th><th>Max</th><th>Mean</th><th>Std. dev.</th></tr>\n");
		featuregroup_stats_t *featuregroups_stats;
		for (int tr=0; tr < features_num; tr++)
		{
			featuregroups_stats = &(split->featuregroups_stats[tr]);
			fprintf(output_file,"<tr><td>%d</td><td>%s</td><td>%.4g</td><td>%.4g</td><td>%.4g</td><td>%.4g</td></tr>\n",
					tr+1, featuregroups_stats->name.c_str(), featuregroups_stats->min, featuregroups_stats->max,
					featuregroups_stats->mean, featuregroups_stats->stddev);
		}
		fprintf(output_file,"</table><br>\n");

		/* individual image predictions */
		if (split->individual_images)
		{  
			char closest_image[256],interpolated_value[256];

			/* add the most similar image if WNN and no tiling */
			if ((split->method==WNN || is_continuous) && featureset->n_samples==1) strcpy(closest_image,"<th>Most similar image</th>");
			else strcpy(closest_image,"");

			fprintf(output_file,"<a href=\"#\" onClick=\"sigs_used=document.getElementById('IndividualImages_split%d'); if (sigs_used.style.display=='none'){ sigs_used.style.display='inline'; } else { sigs_used.style.display='none'; } return false; \">Individual image predictions</a><br>\n",split_index);
			fprintf(output_file,"<TABLE ID=\"IndividualImages_split%d\" border=\"1\" style=\"display: none;\">\n       <tr><th>Image No.</th>",split_index);
			if (!is_continuous) fprintf(output_file,"<th width='100'>Normalization Factor</th>");
			for (class_index=1;class_index<=class_num;class_index++)
				fprintf(output_file,"<th>%s</th>",class_labels[class_index].c_str() );
			if (is_numeric) strcpy(interpolated_value,"<th width='100'>Interpolated Value</th>");
			else strcpy(interpolated_value,"");
			if (is_continuous) fprintf(output_file,"<th>&nbsp;</th><th width='100'>Actual Value</th><th width='100'>Predicted Value</th>");
			else fprintf(output_file,"<th>&nbsp;</th><th width='100'>Actual Class</th><th width='100'>Predicted Class</th><th width='100'>Classification Correctness</th>%s",interpolated_value);
			fprintf(output_file,"<th>Image</th>%s</tr>\n",closest_image);		 
			fprintf(output_file,"%s",split->individual_images);
			fprintf(output_file,"</table><br><br>\n");
		}
	}

	fprintf(output_file,"<br><br><br><br><br><br> \n\n\n\n\n\n\n\n");

	fprintf(output_file,"</CENTER> \n </BODY> \n </HTML>\n");
	return (1);
}

void TrainingSet::Summarize(featureset_t *featureset) {
	int class_index;
// Print out a summary
	if (verbosity>=2) {
		printf ("----------\nSummary of '%s' (%ld samples total, %d samples per image):\n",source_path,count, featureset->sampling_opts.rotations*featureset->sampling_opts.tiles_x*featureset->sampling_opts.tiles_y);
		if (class_num == 1) { // one known class or a continuous class
			if (is_continuous) printf ("%ld samples with numerical values. Interpolation will be done instead of classification\n",class_nsamples[1]);
			else printf ("Single class '%s' with %ld samples. Suitable as a test/classification set only.\n",class_labels[1].c_str(),class_nsamples[1]);
			if (class_nsamples[0]) printf ("%ld unknown samples.\n",class_nsamples[0]);
		} else if (class_num == 0) {
			printf ("%ld unknown samples. Suitable as a test/classification set only.\n",class_nsamples[0]);
		} else {
			if (is_numeric) {
				printf ("'Class label' (interpreted value) number of samples.\n");
				for (class_index=1;class_index<=class_num;class_index++) {
					printf ("'%s'\t(%.3g)\t%ld\n",class_labels[class_index].c_str(),atof(class_labels[class_index].c_str()),class_nsamples[class_index]);
				}
				if (class_nsamples[0]) printf ("UNKNOWN\t(N/A)\t%ld\n",class_nsamples[0]);
				if (is_pure_numeric) printf ("Class labels are purely numeric\n");
			} else {
				printf ("'Class label' number of samples.\n");
				for (class_index=1;class_index<=class_num;class_index++) {
					printf ("'%s'\t%ld\n",class_labels[class_index].c_str(),class_nsamples[class_index]);
				}
				if (class_nsamples[0]) printf ("UNKNOWN\t(N/A)\t%ld\n",class_nsamples[0]);
			}
		}
		printf ("----------\n");
	}
}

#ifdef WIN32
#pragma package(smart_init)
#endif

