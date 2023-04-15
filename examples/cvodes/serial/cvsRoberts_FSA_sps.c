/* -----------------------------------------------------------------
 * Programmer(s): Ting Yan @ SMU
 *      Based on cvsRoberts_FSA_dns.c and modified to use SUPERLU_MT
 * -----------------------------------------------------------------
 * SUNDIALS Copyright Start
 * Copyright (c) 2002-2023, Lawrence Livermore National Security
 * and Southern Methodist University.
 * All rights reserved.
 *
 * See the top-level LICENSE and NOTICE files for details.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SUNDIALS Copyright End
 * -----------------------------------------------------------------
 * Example problem:
 *
 * The following is a simple example problem, with the coding
 * needed for its solution by CVODES for Forward Sensitivity
 * Analysis. The problem is from chemical kinetics, and consists
 * of the following three rate equations:
 *    dy1/dt = -p1*y1 + p2*y2*y3
 *    dy2/dt =  p1*y1 - p2*y2*y3 - p3*(y2)^2
 *    dy3/dt =  p3*(y2)^2
 * on the interval from t = 0.0 to t = 4.e10, with initial
 * conditions: y1 = 1.0, y2 = y3 = 0. The problem is stiff.
 * The reaction rates are: p1=0.04, p2=1e4, and p3=3e7.
 * This program solves the problem with the BDF method, Newton
 * iteration with the SUPERLU_MT linear solver, and a
 * user-supplied Jacobian routine.
 * It uses a scalar relative tolerance and a vector absolute
 * tolerance. Output is printed in decades from t = .4 to t = 4.e10.
 * Run statistics (optional outputs) are printed at the end.
 *
 * Optionally, CVODES can compute sensitivities with respect to the
 * problem parameters p1, p2, and p3.
 * The sensitivity right hand side is given analytically through the
 * user routine fS (of type SensRhs1Fn).
 * Any of three sensitivity methods (SIMULTANEOUS, STAGGERED, and
 * STAGGERED1) can be used and sensitivities may be included in the
 * error test or not (error control set on SUNTRUE or SUNFALSE,
 * respectively).
 *
 * Execution:
 *
 * If no sensitivities are desired:
 *    % cvsRoberts_FSA_sps -nosensi
 * If sensitivities are to be computed:
 *    % cvsRoberts_FSA_sps -sensi sensi_meth err_con
 * where sensi_meth is one of {sim, stg, stg1} and err_con is one of
 * {t, f}.
 * -----------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cvodes/cvodes.h>                  /* prototypes for CVODE fcts., consts.  */
#include <nvector/nvector_serial.h>         /* access to serial N_Vector            */
#include <sunmatrix/sunmatrix_sparse.h>     /* access to sparse SUNMatrix           */
#include <sunlinsol/sunlinsol_superlumt.h>  /* access to SuperLUMT linear solver    */
#include <sundials/sundials_types.h>        /* defs. of realtype, sunindextype      */
#include <sundials/sundials_math.h>         /* defs. of SUNRabs, SUNRexp, etc.      */

/* User-defined vector accessor macro: Ith */

/* These macros are defined in order to write code which exactly matches
   the mathematical problem description given above.

   Ith(v,i) references the ith component of the vector v, where i is in
   the range [1..NEQ] and NEQ is defined below. The Ith macro is defined
   using the N_VIth macro in nvector.h. N_VIth numbers the components of
   a vector starting from 0. */

#define Ith(v,i)    NV_Ith_S(v,i-1)         /* i-th vector component, i=1..NEQ */


/* Problem Constants */

#define NEQ   3                /* number of equations  */
#define NNZ   7                /* number of non-zero entries in the Jacobian */
#define Y1    RCONST(1.0)      /* initial y components */
#define Y2    RCONST(0.0)
#define Y3    RCONST(0.0)
#define RTOL  RCONST(1.0e-4)   /* scalar relative tolerance            */
#define ATOL1 RCONST(1.0e-8)   /* vector absolute tolerance components */
#define ATOL2 RCONST(1.0e-14)
#define ATOL3 RCONST(1.0e-6)
#define T0    RCONST(0.0)      /* initial time           */
#define T1    RCONST(0.4)      /* first output time      */
#define TMULT RCONST(10.0)     /* output time factor     */
#define NOUT  12               /* number of output times */

#define NP    3             /* number of problem parameters */
#define NS    3             /* number of sensitivities computed */

#define ZERO  RCONST(0.0)

/* Type : UserData */

typedef struct {
  realtype p[3];           /* problem parameters */
} *UserData;

/* Functions Called by the Solver */

static int f(realtype t, N_Vector y, N_Vector ydot, void *user_data);

static int Jac(realtype t, N_Vector y, N_Vector fy, SUNMatrix J,
               void *user_data, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3);

static int fS(int Ns, realtype t, N_Vector y, N_Vector ydot,
              int iS, N_Vector yS, N_Vector ySdot,
              void *user_data, N_Vector tmp1, N_Vector tmp2);

static int ewt(N_Vector y, N_Vector w, void *user_data);

/* Private functions to output results */

static void PrintOutput(void *cvode_mem, realtype t, N_Vector u);
static void PrintOutputS(N_Vector *uS);

/* Private function to print final statistics */

static void PrintFinalStats(void *cvode_mem, booleantype sensi);

/* Prototypes of private functions */

static void ProcessArgs(int argc, char *argv[],
                        booleantype *sensi, int *sensi_meth,
                        booleantype *err_con);

static void WrongArgs(char *name);

static int check_retval(void *returnvalue, const char *funcname, int opt);


/*
 *-------------------------------
 * Main Program
 *-------------------------------
 */

int main(int argc, char *argv[])
{
  SUNContext sunctx;
  realtype t, tout;
  N_Vector y;
  SUNMatrix A;
  SUNLinearSolver LS;
  void *cvode_mem;
  int retval, iout;
  UserData data;

  realtype pbar[NS];
  int is;
  N_Vector *yS;
  booleantype sensi, err_con;
  int sensi_meth;

  data = NULL;
  y = NULL;
  yS = NULL;
  A = NULL;
  LS = NULL;
  cvode_mem = NULL;

  /* Process arguments */
  ProcessArgs(argc, argv, &sensi, &sensi_meth, &err_con);

  /* User data structure */
  data = (UserData) malloc(sizeof *data);
  if (check_retval((void *)data, "malloc", 2)) return(1);
  data->p[0] = RCONST(0.04);
  data->p[1] = RCONST(1.0e4);
  data->p[2] = RCONST(3.0e7);

  /* Create the SUNDIALS context that all SUNDIALS objects require */
  retval = SUNContext_Create(NULL, &sunctx);
  if (check_retval(&retval, "SUNContext_Create", 1)) return(1);

  /* Initial conditions */
  y = N_VNew_Serial(NEQ, sunctx);
  if (check_retval((void *)y, "N_VNew_Serial", 0)) return(1);

  /* Initialize y */
  Ith(y,1) = Y1;
  Ith(y,2) = Y2;
  Ith(y,3) = Y3;

  /* Call CVodeCreate to create the solver memory and specify the
   * Backward Differentiation Formula */
  cvode_mem = CVodeCreate(CV_BDF, sunctx);
  if (check_retval((void *)cvode_mem, "CVodeCreate", 0)) return(1);

  /* Call CVodeInit to initialize the integrator memory and specify the
   * user's right hand side function in y'=f(t,y), the initial time T0, and
   * the initial dependent variable vector y. */
  retval = CVodeInit(cvode_mem, f, T0, y);
  if (check_retval(&retval, "CVodeInit", 1)) return(1);

  /* Call CVodeWFtolerances to specify a user-supplied function ewt that sets
   * the multiplicative error weights w_i for use in the weighted RMS norm */
  retval = CVodeWFtolerances(cvode_mem, ewt);
  if (check_retval(&retval, "CVodeWFtolerances", 1)) return(1);

  /* Attach user data */
  retval = CVodeSetUserData(cvode_mem, data);
  if (check_retval(&retval, "CVodeSetUserData", 1)) return(1);

  /* Create sparse SUNMatrix for use in linear solves */
  A = SUNSparseMatrix(NEQ, NEQ, NNZ, CSC_MAT, sunctx);
  if (check_retval((void *)A, "SUNSparseMatrix", 0)) return(1);

  /* Create SuperLUMT solver object for use by CVode (one thread) */
  LS = SUNLinSol_SuperLUMT(y, A, 1, sunctx);
  if (check_retval((void *)LS, "SUNLinSol_SuperLUMT", 0)) return(1);

  /* Attach the matrix and linear solver */
  retval = CVodeSetLinearSolver(cvode_mem, LS, A);
  if (check_retval(&retval, "CVodeSetLinearSolver", 1)) return(1);

  /* Set the user-supplied Jacobian routine Jac */
  retval = CVodeSetJacFn(cvode_mem, Jac);
  if (check_retval(&retval, "CVodeSetJacFn", 1)) return(1);

  printf(" \n3-species kinetics problem\n");

  /* Sensitivity-related settings */
  if (sensi) {

    /* Set parameter scaling factor */
    pbar[0] = data->p[0];
    pbar[1] = data->p[1];
    pbar[2] = data->p[2];

    /* Set sensitivity initial conditions */
    yS = N_VCloneVectorArray(NS, y);
    if (check_retval((void *)yS, "N_VCloneVectorArray", 0)) return(1);
    for (is=0;is<NS;is++) N_VConst(ZERO, yS[is]);

    /* Call CVodeSensInit1 to activate forward sensitivity computations
     * and allocate internal memory for COVEDS related to sensitivity
     * calculations. Computes the right-hand sides of the sensitivity
     * ODE, one at a time */
    retval = CVodeSensInit1(cvode_mem, NS, sensi_meth, fS, yS);
    if(check_retval(&retval, "CVodeSensInit", 1)) return(1);

    /* Call CVodeSensEEtolerances to estimate tolerances for sensitivity
     * variables based on the rolerances supplied for states variables and
     * the scaling factor pbar */
    retval = CVodeSensEEtolerances(cvode_mem);
    if(check_retval(&retval, "CVodeSensEEtolerances", 1)) return(1);

    /* Set sensitivity analysis optional inputs */
    /* Call CVodeSetSensErrCon to specify the error control strategy for
     * sensitivity variables */
    retval = CVodeSetSensErrCon(cvode_mem, err_con);
    if (check_retval(&retval, "CVodeSetSensErrCon", 1)) return(1);

    /* Call CVodeSetSensParams to specify problem parameter information for
     * sensitivity calculations */
    retval = CVodeSetSensParams(cvode_mem, NULL, pbar, NULL);
    if (check_retval(&retval, "CVodeSetSensParams", 1)) return(1);

    printf("Sensitivity: YES ");
    if(sensi_meth == CV_SIMULTANEOUS)
      printf("( SIMULTANEOUS +");
    else
      if(sensi_meth == CV_STAGGERED) printf("( STAGGERED +");
      else                           printf("( STAGGERED1 +");
    if(err_con) printf(" FULL ERROR CONTROL )");
    else        printf(" PARTIAL ERROR CONTROL )");

  } else {

    printf("Sensitivity: NO ");

  }

  /* In loop, call CVode, print results, and test for error.
     Break out of loop when NOUT preset output times have been reached.  */

  printf("\n\n");
  printf("===========================================");
  printf("============================\n");
  printf("     T     Q       H      NST           y1");
  printf("           y2           y3    \n");
  printf("===========================================");
  printf("============================\n");

  for (iout=1, tout=T1; iout <= NOUT; iout++, tout *= TMULT) {

    retval = CVode(cvode_mem, tout, y, &t, CV_NORMAL);
    if (check_retval(&retval, "CVode", 1)) break;

    PrintOutput(cvode_mem, t, y);

    /* Call CVodeGetSens to get the sensitivity solution vector after a
     * successful return from CVode */
    if (sensi) {
      retval = CVodeGetSens(cvode_mem, &t, yS);
      if (check_retval(&retval, "CVodeGetSens", 1)) break;
      PrintOutputS(yS);
    }
    printf("-----------------------------------------");
    printf("------------------------------\n");

  }

  /* Print some final statistics */
  PrintFinalStats(cvode_mem, sensi);

  /* Free memory */
  N_VDestroy(y);                            /* Free y vector */
  if (sensi) N_VDestroyVectorArray(yS, NS); /* Free yS vector */
  free(data);                               /* Free user data */
  CVodeFree(&cvode_mem);                    /* Free CVODES memory */
  SUNLinSolFree(LS);                        /* Free the linear solver memory */
  SUNMatDestroy(A);                         /* Free the matrix memory */
  SUNContext_Free(&sunctx);                 /* Free the SUNDIALS context */

  return(0);
}


/*
 *-------------------------------
 * Functions called by the solver
 *-------------------------------
 */

/*
 * f routine. Compute function f(t,y).
 */

static int f(realtype t, N_Vector y, N_Vector ydot, void *user_data)
{
  realtype y1, y2, y3, yd1, yd3;
  UserData data;
  realtype p1, p2, p3;

  y1 = Ith(y,1); y2 = Ith(y,2); y3 = Ith(y,3);
  data = (UserData) user_data;
  p1 = data->p[0]; p2 = data->p[1]; p3 = data->p[2];

  yd1 = Ith(ydot,1) = -p1*y1 + p2*y2*y3;
  yd3 = Ith(ydot,3) = p3*y2*y2;
        Ith(ydot,2) = -yd1 - yd3;

  return(0);
}

/*
 * Jacobian routine. Compute J(t,y) = df/dy. *
 */

static int Jac(realtype t, N_Vector y, N_Vector fy, SUNMatrix J,
               void *user_data, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3)
{
  /* State at which to evaluate the Jacobian */
  realtype *yval = N_VGetArrayPointer(y);

  /* J is stored in CSC format:
     data    = non-zero matrix entries stored column-wise (length NNZ)
     rowvals = row index for each non-zero matrix entry (length NNZ)
     colptrs = i-th entry is the index in data where the first non-zero matrix
               entry of the i-th column is stored (length NEQ + 1) */
  realtype     *data    = SUNSparseMatrix_Data(J);
  sunindextype *rowvals = SUNSparseMatrix_IndexValues(J);
  sunindextype *colptrs = SUNSparseMatrix_IndexPointers(J);
  UserData     userdata = (UserData) user_data;
  realtype     p1 = userdata->p[0];
  realtype     p2 = userdata->p[1];
  realtype     p3 = userdata->p[2];

  /* first column entries start at data[0], two entries (rows 0 and 1) */
  colptrs[0] = 0;

  rowvals[0] = 0;
  data[0]    = -p1;

  rowvals[1] = 1;
  data[1]    = p1;

  /* second column entries start at data[2], three entries (rows 0, 1, and 2) */
  colptrs[1] = 2;

  rowvals[2] = 0;
  data[2]    = p2 * yval[2];

  rowvals[3] = 1;
  data[3]    = (-p2 * yval[2]) - (2 * p3 * yval[1]);

  rowvals[4] = 2;
  data[4]    = 2 * p3 * yval[1];

  /* third column entries start at data[5], two entries (rows 0 and 1) */
  colptrs[2] = 5;

  rowvals[5] = 0;
  data[5]    = p2 * yval[1];

  rowvals[6] = 1;
  data[6]    = -p2 * yval[1];

  /* number of non-zeros */
  colptrs[3] = 7;

  return(0);
}

/*
 * fS routine. Compute sensitivity r.h.s.
 */

static int fS(int Ns, realtype t, N_Vector y, N_Vector ydot,
              int iS, N_Vector yS, N_Vector ySdot,
              void *user_data, N_Vector tmp1, N_Vector tmp2)
{
  UserData data;
  realtype p1, p2, p3;
  realtype y1, y2, y3;
  realtype s1, s2, s3;
  realtype sd1, sd2, sd3;

  data = (UserData) user_data;
  p1 = data->p[0]; p2 = data->p[1]; p3 = data->p[2];

  y1 = Ith(y,1);  y2 = Ith(y,2);  y3 = Ith(y,3);
  s1 = Ith(yS,1); s2 = Ith(yS,2); s3 = Ith(yS,3);

  sd1 = -p1*s1 + p2*y3*s2 + p2*y2*s3;
  sd3 = 2*p3*y2*s2;
  sd2 = -sd1-sd3;

  switch (iS) {
  case 0:
    sd1 += -y1;
    sd2 +=  y1;
    break;
  case 1:
    sd1 +=  y2*y3;
    sd2 += -y2*y3;
    break;
  case 2:
    sd2 += -y2*y2;
    sd3 +=  y2*y2;
    break;
  }

  Ith(ySdot,1) = sd1;
  Ith(ySdot,2) = sd2;
  Ith(ySdot,3) = sd3;

  return(0);
}

/*
 * EwtSet function. Computes the error weights at the current solution.
 */

static int ewt(N_Vector y, N_Vector w, void *user_data)
{
  int i;
  realtype yy, ww, rtol, atol[3];

  rtol    = RTOL;
  atol[0] = ATOL1;
  atol[1] = ATOL2;
  atol[2] = ATOL3;

  for (i=1; i<=3; i++) {
    yy = Ith(y,i);
    ww = rtol * SUNRabs(yy) + atol[i-1];
    if (ww <= 0.0) return (-1);
    Ith(w,i) = 1.0/ww;
  }

  return(0);
}

/*
 *-------------------------------
 * Private helper functions
 *-------------------------------
 */

/*
 * Process and verify arguments to cvsfwddenx.
 */

static void ProcessArgs(int argc, char *argv[],
                        booleantype *sensi, int *sensi_meth, booleantype *err_con)
{
  *sensi = SUNFALSE;
  *sensi_meth = -1;
  *err_con = SUNFALSE;

  if (argc < 2) WrongArgs(argv[0]);

  if (strcmp(argv[1],"-nosensi") == 0)
    *sensi = SUNFALSE;
  else if (strcmp(argv[1],"-sensi") == 0)
    *sensi = SUNTRUE;
  else
    WrongArgs(argv[0]);

  if (*sensi) {

    if (argc != 4)
      WrongArgs(argv[0]);

    if (strcmp(argv[2],"sim") == 0)
      *sensi_meth = CV_SIMULTANEOUS;
    else if (strcmp(argv[2],"stg") == 0)
      *sensi_meth = CV_STAGGERED;
    else if (strcmp(argv[2],"stg1") == 0)
      *sensi_meth = CV_STAGGERED1;
    else
      WrongArgs(argv[0]);

    if (strcmp(argv[3],"t") == 0)
      *err_con = SUNTRUE;
    else if (strcmp(argv[3],"f") == 0)
      *err_con = SUNFALSE;
    else
      WrongArgs(argv[0]);
  }

}

static void WrongArgs(char *name)
{
    printf("\nUsage: %s [-nosensi] [-sensi sensi_meth err_con]\n",name);
    printf("         sensi_meth = sim, stg, or stg1\n");
    printf("         err_con    = t or f\n");

    exit(0);
}

/*
 * Print current t, step count, order, stepsize, and solution.
 */

static void PrintOutput(void *cvode_mem, realtype t, N_Vector u)
{
  long int nst;
  int qu, retval;
  realtype hu, *udata;

  udata = N_VGetArrayPointer(u);

  retval = CVodeGetNumSteps(cvode_mem, &nst);
  check_retval(&retval, "CVodeGetNumSteps", 1);
  retval = CVodeGetLastOrder(cvode_mem, &qu);
  check_retval(&retval, "CVodeGetLastOrder", 1);
  retval = CVodeGetLastStep(cvode_mem, &hu);
  check_retval(&retval, "CVodeGetLastStep", 1);

#if defined(SUNDIALS_EXTENDED_PRECISION)
  printf("%8.3Le %2d  %8.3Le %5ld\n", t, qu, hu, nst);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
  printf("%8.3e %2d  %8.3e %5ld\n", t, qu, hu, nst);
#else
  printf("%8.3e %2d  %8.3e %5ld\n", t, qu, hu, nst);
#endif

  printf("                  Solution       ");

#if defined(SUNDIALS_EXTENDED_PRECISION)
  printf("%12.4Le %12.4Le %12.4Le \n", udata[0], udata[1], udata[2]);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
  printf("%12.4e %12.4e %12.4e \n", udata[0], udata[1], udata[2]);
#else
  printf("%12.4e %12.4e %12.4e \n", udata[0], udata[1], udata[2]);
#endif

}

/*
 * Print sensitivities.
*/

static void PrintOutputS(N_Vector *uS)
{
  realtype *sdata;

  sdata = N_VGetArrayPointer(uS[0]);
  printf("                  Sensitivity 1  ");

#if defined(SUNDIALS_EXTENDED_PRECISION)
  printf("%12.4Le %12.4Le %12.4Le \n", sdata[0], sdata[1], sdata[2]);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
  printf("%12.4e %12.4e %12.4e \n", sdata[0], sdata[1], sdata[2]);
#else
  printf("%12.4e %12.4e %12.4e \n", sdata[0], sdata[1], sdata[2]);
#endif

  sdata = N_VGetArrayPointer(uS[1]);
  printf("                  Sensitivity 2  ");

#if defined(SUNDIALS_EXTENDED_PRECISION)
  printf("%12.4Le %12.4Le %12.4Le \n", sdata[0], sdata[1], sdata[2]);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
  printf("%12.4e %12.4e %12.4e \n", sdata[0], sdata[1], sdata[2]);
#else
  printf("%12.4e %12.4e %12.4e \n", sdata[0], sdata[1], sdata[2]);
#endif

  sdata = N_VGetArrayPointer(uS[2]);
  printf("                  Sensitivity 3  ");

#if defined(SUNDIALS_EXTENDED_PRECISION)
  printf("%12.4Le %12.4Le %12.4Le \n", sdata[0], sdata[1], sdata[2]);
#elif defined(SUNDIALS_DOUBLE_PRECISION)
  printf("%12.4e %12.4e %12.4e \n", sdata[0], sdata[1], sdata[2]);
#else
  printf("%12.4e %12.4e %12.4e \n", sdata[0], sdata[1], sdata[2]);
#endif
}

/*
 * Get and print some final statistics
 */

static void PrintFinalStats(void *cvode_mem, booleantype sensi)
{
  long int nst, nfe, nsetups, nje, nni, nnf, ncfn, netf;
  long int nfSe, nfeS, nsetupsS, nniS, nnfS, ncfnS, netfS;
  int retval;

  retval = CVodeGetNumSteps(cvode_mem, &nst);
  check_retval(&retval, "CVodeGetNumSteps", 1);
  retval = CVodeGetNumRhsEvals(cvode_mem, &nfe);
  check_retval(&retval, "CVodeGetNumRhsEvals", 1);
  retval = CVodeGetNumLinSolvSetups(cvode_mem, &nsetups);
  check_retval(&retval, "CVodeGetNumLinSolvSetups", 1);
  retval = CVodeGetNumErrTestFails(cvode_mem, &netf);
  check_retval(&retval, "CVodeGetNumErrTestFails", 1);
  retval = CVodeGetNumNonlinSolvIters(cvode_mem, &nni);
  check_retval(&retval, "CVodeGetNumNonlinSolvIters", 1);
  retval = CVodeGetNumNonlinSolvConvFails(cvode_mem, &nnf);
  check_retval(&retval, "CVodeGetNumNonlinSolvConvFails", 1);
  retval = CVodeGetNumStepSolveFails(cvode_mem, &ncfn);
  check_retval(&retval, "CVodeGetNumStepSolveFails", 1);

  if (sensi) {
    retval = CVodeGetSensNumRhsEvals(cvode_mem, &nfSe);
    check_retval(&retval, "CVodeGetSensNumRhsEvals", 1);
    retval = CVodeGetNumRhsEvalsSens(cvode_mem, &nfeS);
    check_retval(&retval, "CVodeGetNumRhsEvalsSens", 1);
    retval = CVodeGetSensNumLinSolvSetups(cvode_mem, &nsetupsS);
    check_retval(&retval, "CVodeGetSensNumLinSolvSetups", 1);
    retval = CVodeGetSensNumErrTestFails(cvode_mem, &netfS);
    check_retval(&retval, "CVodeGetSensNumErrTestFails", 1);
    retval = CVodeGetSensNumNonlinSolvIters(cvode_mem, &nniS);
    check_retval(&retval, "CVodeGetSensNumNonlinSolvIters", 1);
    retval = CVodeGetSensNumNonlinSolvConvFails(cvode_mem, &nnfS);
    check_retval(&retval, "CVodeGetSensNumNonlinSolvConvFails", 1);
    retval = CVodeGetNumStepSensSolveFails(cvode_mem, &ncfnS);
    check_retval(&retval, "CVodeGetNumStepSensSolveFails", 1);
  }

  retval = CVodeGetNumJacEvals(cvode_mem, &nje);
  check_retval(&retval, "CVodeGetNumJacEvals", 1);

  printf("\nFinal Statistics:\n");
  printf("nst = %-6ld nfe = %-6ld nsetups = %-6ld nje = %ld\n",
         nst, nfe, nsetups, nje);
  printf("nni = %-6ld nnf = %-6ld netf = %-6ld    ncfn = %-6ld\n\n",
         nni, nnf, netf, ncfn);

  if(sensi) {
    printf("nfSe = %-6ld nfeS = %-6ld nsetupsS = %-6ld\n",
           nfSe, nfeS, nsetupsS);
    printf("nniS = %-6ld nnfS = %-6ld netfS = %-6ld ncfnS = %-6ld\n\n",
           nniS, nnfS, netfS, ncfnS);
  }
}

/*
 * Check function return value...
 *   opt == 0 means SUNDIALS function allocates memory so check if
 *            returned NULL pointer
 *   opt == 1 means SUNDIALS function returns an integer value so check if
 *            retval < 0
 *   opt == 2 means function allocates memory so check if returned
 *            NULL pointer
 */

static int check_retval(void *returnvalue, const char *funcname, int opt)
{
  int *retval;

  /* Check if SUNDIALS function returned NULL pointer - no memory allocated */
  if (opt == 0 && returnvalue == NULL) {
    fprintf(stderr, "\nSUNDIALS_ERROR: %s() failed - returned NULL pointer\n\n",
            funcname);
    return(1); }

  /* Check if retval < 0 */
  else if (opt == 1) {
    retval = (int *) returnvalue;
    if (*retval < 0) {
      fprintf(stderr, "\nSUNDIALS_ERROR: %s() failed with retval = %d\n\n",
              funcname, *retval);
      return(1); }}

  /* Check if function returned NULL pointer - no memory allocated */
  else if (opt == 2 && returnvalue == NULL) {
    fprintf(stderr, "\nMEMORY_ERROR: %s() failed - returned NULL pointer\n\n",
            funcname);
    return(1); }

  return(0);
}
