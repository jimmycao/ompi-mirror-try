/*
 * $HEADER$
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "support.h"

/**
 * A testing support library to provide uniform reporting output
 */

static int lam_n_tests;
static int lam_n_success;
static int lam_n_failures;
static char *lam_description;

void test_init(char *a)
{
    /* local variables */
    size_t len;

    /* save the descriptive string */
    len=strlen(a);
    lam_description=(char *)malloc(len+1);
    assert(lam_description);

    strcpy(lam_description,a);

    /* initialize counters */
    lam_n_tests=0;
    lam_n_success=0;
    lam_n_failures=0;

    return;

}

void test_success(void){

    lam_n_tests++;
    lam_n_success++;

    return;
}

void test_failure(char *a){
    lam_n_tests++;
    lam_n_failures++;

    fprintf(stderr," Failure : %s \n",a);
    fflush(stderr);
}

int test_verify_str(const char *expected_result, const char *test_result){

    /* local variables */
    size_t len_expect,len_result;
    int return_value;

    return_value = 1;
    len_expect = expected_result ? strlen(expected_result) : 0;
    len_result = test_result ? strlen(test_result) : 0;

    if( (!(len_expect == len_result)) ||
            ( 0 != strcmp(expected_result,test_result)) ) {
        test_failure("Comparison failure");
        fprintf(stderr," Expected result: %s \n", expected_result);
        fprintf(stderr," Test result: %s \n", test_result);
        fflush(stderr);
        return_value = 0;
    }
    else
    {
        test_success();
    }

    return return_value;
}

int test_verify_int(int expected_result, int test_result)
{
    /* local variables */
    int return_value;
    
    return_value = 1;
    if( expected_result != test_result ) 
    {
        test_failure("Comparison failure");
        fprintf(stderr," Expected result: %d \n", expected_result);
        fprintf(stderr," Test result: %d \n", test_result);
        fflush(stderr);
        return_value = 0;
    }
    else
    {
        test_success();
    }
        
    return return_value;    
}

int test_finalize(void)
{

    int return_value;

    return_value=1;

    if( lam_n_tests == lam_n_success) {
        fprintf(stderr," SUPPORT :: LAM Test Passed :: %s %d tests \n",
                lam_description,lam_n_tests);
        fflush(stderr);
    } else {
        fprintf(stderr," SUPPORT :: LAM Test failed :: %s :: %d of %d failed\n"
                ,lam_description,lam_n_failures,lam_n_tests);
        fflush(stderr);
        return_value=0;
    }

    return return_value;
}

