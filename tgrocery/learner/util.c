#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "linear.h"
#define Malloc(type,n) (type *)malloc((n)*sizeof(type))


static char *line = NULL;
static INT64 max_line_len;

static char* readline(FILE *input)
{
	INT64 len;
	
	if(fgets(line,max_line_len,input) == NULL)
		return NULL;

	while(strrchr(line,'\n') == NULL)
	{
		max_line_len *= 2;
		line = (char *) realloc(line,max_line_len);
		len = (INT64) strlen(line);
		if(fgets(line+len,max_line_len-len,input) == NULL)
			break;
	}
	return line;
}

typedef struct {
	struct problem prob;
	struct feature_node* x_space;
	INT64 len_x_space;
} SVMProblem;

void freeSVMProblem(SVMProblem svmprob) {
	struct problem *prob = &(svmprob.prob);
	if (prob->x!=NULL) free(prob->x);
	if (prob->y!=NULL) free(prob->y);
	if (svmprob.x_space!=NULL) free(svmprob.x_space);
}


// read in a problem (in libsvm format)
SVMProblem read_problem(const char *filename, double bias, INT64 *error_code)
{
	INT64 max_index, inst_max_index, i;
	INT64 elements, j;
	FILE *fp = fopen(filename,"r");
	char *endptr;
	char *idx, *val, *label;
	struct problem prob;
	SVMProblem svmprob;
	struct feature_node* x_space;

	/**
	 * error_code:
	 * 0	no error
	 * > 0	input format error. The error_code value
	 * 	indicates the line number.
	 * -1	can not open file
	 * -2	memory exhausted
	 */
	*error_code = 0;

	if(fp == NULL)
	{
		*error_code = -1;
		return svmprob;
	}

	prob.l = 0;
	elements = 0;
	max_line_len = 1024;
	line = Malloc(char,max_line_len);
	while(readline(fp)!=NULL)
	{
		char *p = strtok(line," \t"); // label

		// features
		while(1)
		{
			p = strtok(NULL," \t");
			if(p == NULL || *p == '\n') // check '\n' as ' ' may be after the last feature
				break;
			elements++;
		}
		prob.l++;
	}
	rewind(fp);

	prob.bias=bias;
	if(prob.bias >= 0) elements += prob.l;

	errno = 0;
	prob.y = Malloc(double,prob.l);
	prob.x = Malloc(struct feature_node *,prob.l);
    x_space = Malloc(struct feature_node,elements+prob.l);

	if(errno == ENOMEM)
	{
		free(line);
		fclose(fp);
		*error_code = -2;
		return svmprob;
	}

	max_index = 0;
	j=0;
	for(i=0;i<prob.l;i++)
	{
		inst_max_index = 0; // strtol gives 0 if wrong format
		readline(fp);
		prob.x[i] = &x_space[j];
		label = strtok(line," \t\n");
		printf(line);
		if(label == NULL) // empty line
		{	
			free(line);
			fclose(fp);
			*error_code = i+1;
			return svmprob;
		}

		prob.y[i] = strtod(label,&endptr);
		if(endptr == label || *endptr != '\0')
		{
			free(line);
			fclose(fp);
			*error_code = i+1;
			return svmprob;
		}

		while(1)
		{
			idx = strtok(NULL,":");
			val = strtok(NULL," \t");

			if(val == NULL)
				break;

			errno = 0;
			// strtoll doesn't work under 32-bit, replace with strtol
			x_space[j].index = (INT64)strtoll(idx,&endptr,10);
			if(endptr == idx || errno != 0 || *endptr != '\0' || x_space[j].index <= inst_max_index)
			{	
				free(line);
				fclose(fp);
				*error_code = i+1;
				return svmprob;
			}
			else
				inst_max_index = x_space[j].index;

			errno = 0;
			x_space[j].value = strtod(val,&endptr);
			//if(binary) x_space[j].value = x_space[j].value != 0;
			if(endptr == val || errno != 0 || (*endptr != '\0' && !isspace(*endptr)))
			{	
				free(line);
				fclose(fp);
				*error_code = i+1;
				return svmprob;
			}

			++j;
		}

		if(inst_max_index > max_index)
			max_index = inst_max_index;

		if(prob.bias >= 0)
			x_space[j++].value = prob.bias;

		x_space[j++].index = -1;
	}

	if(prob.bias >= 0)
	{
		prob.n=max_index+1;
		for(i=1;i<prob.l;i++)
			(prob.x[i]-2)->index = prob.n; 
		x_space[j-2].index = prob.n;
	}
	else
		prob.n=max_index;

	fclose(fp);
	free(line);

	svmprob.prob = prob;
	svmprob.x_space = x_space;
	svmprob.len_x_space = j;

	return svmprob;
}

void normalize(struct problem *prob, int binary, int norm, int tf, int idf, double* idf_val)
{
	INT64 i;

	for(i = 0; i < prob->l; ++i)
	{
		struct feature_node* xi;

		if(binary)
		{
			xi = prob->x[i];
			while(xi->index != -1)
			{
				xi->value = xi->value != 0;
				++xi;
			}
		}

		if(tf)
		{
			double norm = 0;
			xi = prob->x[i];
			while(xi->index != -1)
			{
				norm += xi->value;
				++xi;
			}

			xi = prob->x[i];
			if(norm != 0)
				while(xi->index != -1)
				{
					xi->value /= norm;
					++xi;
				}
		}

		if(idf)
		{
			xi = prob->x[i];
			while(xi->index != -1)
			{
				xi->value *= idf_val[xi->index-1];
				++xi;
			}
		}

		if(norm)
		{
			double norm = 0;
			xi = prob->x[i];
			while(xi->index != -1)
			{
				norm += xi->value * xi->value;
				++xi;
			}

			norm = sqrt(norm);

			xi = prob->x[i];
			if(norm != 0)
				while(xi->index != -1)
				{
					xi->value /= norm;
					++xi;
				}
		}
	}
}