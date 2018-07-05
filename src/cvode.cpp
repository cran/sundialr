
#include <Rcpp.h>

#include <cvode/cvode.h>               /* prototypes for CVODE fcts., consts. */
#include <nvector/nvector_serial.h>    /* serial N_Vector types, fcts., macros */
#include <cvode/cvode_direct.h>         /* prototype for CVDense */
// #include <sundials/sundials_dense.h>   /* definitions DlsMat DENSE_ELEM */
#include <sundials/sundials_types.h>   /* definition of type realtype */
#include <sunmatrix/sunmatrix_dense.h>
#include <sunlinsol/sunlinsol_dense.h>

#include <check_flag.h>
#include "check_flag.c"

using namespace Rcpp;

//-- typedefs for RHS function pointer input in from R -------------------------
typedef NumericVector (*funcPtr) (double t, NumericVector y);
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//-- user data is passed to CVODE in a struct, one component is the pointer to
//-- RHS function---------------------------------------------------------------
struct rhs_data{
  SEXP rhs_eqn;
};

struct rhs_data my_rhs = {R_NilValue}; // sets rhs_eqn to NULL
struct rhs_data *my_rhs_ptr = &my_rhs;
//------------------------------------------------------------------------------

//---RHS function that is an input to CVODE, takes user input of RHS in user_data
int rhs_fun(realtype t, N_Vector y, N_Vector ydot, void* user_data){

  // convert y to NumericVector y1
  int y_len = NV_LENGTH_S(y);

  NumericVector y1(y_len);    // filled with zeros
  realtype *y_ptr = N_VGetArrayPointer(y);
  for (int i = 0; i < y_len; i++){
    y1[i] = y_ptr[i];
  }

  // convert ydot to NumericVector ydot1
  int ydot_len = NV_LENGTH_S(ydot);

  NumericVector ydot1(ydot_len);    // filled with zeros

  // cast void pointer to struct and assign rhs to a SEXP
  struct rhs_data *my_rhs_ptr = (struct rhs_data*)user_data;
  SEXP rhs_sexp = (*my_rhs_ptr).rhs_eqn;

  // use function pointer to get the derivatives
  XPtr<funcPtr> rhs_xptr(rhs_sexp);
  funcPtr rhs_fun = *rhs_xptr;

  // use the function to calculate value of RHS ----
  ydot1 = rhs_fun(t, y1);

  // convert NumericVector ydot1 to N_Vector ydot
  realtype *ydot_ptr = N_VGetArrayPointer(ydot);
  for (int i = 0; i<ydot1.length(); i++){
    ydot_ptr[i] = ydot1[i];
  }

  // N_VPrint_Serial(ydot);
  // Rprintf("First value in ydot is %g\n", ydot_ptr[0]);
  // Rprintf("Second value in ydot is %g\n", ydot_ptr[1]);
  // Rprintf("Third value in ydot is %g\n", ydot_ptr[2]);
  // Rprintf("ydot length is %d\n", N_VGetLength_Serial(ydot));

  // everything went smoothly
  return(0);
}
//---RHS function definition ends ----------------------------------------------

//------------------------------------------------------------------------------
//'cvode
//'
//' CVODE solver to solve stiff ODEs
//'@param time_vec time vector
//'@param IC Initial Conditions
//'@param xpsexp External pointer to RHS function
//'@param reltolerance Relative Tolerance (a scalar)
//'@param abstolerance Absolute Tolerance (a vector with length equal to ydot)
//'@example /inst/examples/cv_Roberts_dns.r
// [[Rcpp::export]]
NumericMatrix cvode(NumericVector time_vec, NumericVector IC, SEXP xpsexp,
                    double reltolerance, NumericVector abstolerance){


  int time_vec_len = time_vec.length();
  int y_len = IC.length();
  int abstol_len = abstolerance.length();

  // Rprintf("Time vec length is %d\n", time_vec_len);
  // Rprintf("IC length is %d\n", y_len);
  // Rprintf("abstolerance length is %d\n", abstol_len);

  int flag;
  realtype reltol = reltolerance;
  N_Vector abstol, y0;
  y0 = abstol = NULL;
  //
  realtype T0 = RCONST(time_vec[0]);     //RCONST(0.0);  // Initial Time
  //
  // // realtype iout;
  SUNMatrix SM;
  SUNLinearSolver LS;
  SM = NULL;
  LS = NULL;

  double time;
  int NOUT = time_vec.length();

  // Set the vector absolute tolerance -----------------------------------------
  // abstol must be same length as IC
  abstol = N_VNew_Serial(abstol_len);
  realtype *abstol_ptr = N_VGetArrayPointer(abstol);
  if(abstol_len == 1){
    // if a scalar is provided - use it to make a vector with same values
    for (int i = 0; i<y_len; i++){
      abstol_ptr[i] = abstolerance[0];
    }
  }
  else if (abstol_len == y_len){

    for (int i = 0; i<abstol_len; i++){
      abstol_ptr[i] = abstolerance[i];
    }
  }
  else if(abstol_len != 1 || abstol_len != y_len){

    stop("Absolute tolerance must be a scalar or a vector of same length as IC \n");
  }
  //----------------------------------------------------------------------------
  // Rprintf("First value in abstol is %g\n", abstol_ptr[0]);
  // Rprintf("Second value in abstol is %g\n", abstol_ptr[1]);
  // Rprintf("Third value in abstol is %g\n", abstol_ptr[2]);
  // Rprintf("Abstol length is %d\n", N_VGetLength_Serial(abstol));
  // N_VPrint_Serial(abstol);

  // Set the initial conditions-------------------------------------------------
  y0 = N_VNew_Serial(3);
  realtype *y0_ptr = N_VGetArrayPointer(y0);
  for (int i = 0; i<y_len; i++){
    y0_ptr[i] = IC[i]; // NV_Ith_S(y0, i)
  }
  //----------------------------------------------------------------------------
  // N_VPrint_Serial(y0);
  // Rprintf("First value in y0 is %g\n", y0_ptr[0]);
  // Rprintf("Second value in y0 is %g\n", y0_ptr[1]);
  // Rprintf("Third value in y0 is %g\n", y0_ptr[2]);
  // Rprintf("y0 length is %d\n", N_VGetLength_Serial(y0));

  void *cvode_mem;
  cvode_mem = NULL;

  // Call CVodeCreate to create the solver memory and specify the Backward Differentiation Formula
  // and the use of a Newton iteration
  cvode_mem = CVodeCreate(CV_BDF, CV_NEWTON);
  if (check_flag((void *) cvode_mem, "CVodeCreate", 0)) {
    return (1);
  }

  //-- assign user input to the rhs-data struct
  (*my_rhs_ptr).rhs_eqn = xpsexp;

  // setting the user_data in rhs function
  flag = CVodeSetUserData(cvode_mem, (void*)&my_rhs);
  if (check_flag(&flag, "CVodeSetUserData", 1)) {
    return (1);
  }

  flag = CVodeInit(cvode_mem, rhs_fun, T0, y0);
  if (check_flag(&flag, "CVodeInit", 1)) {
    return (1);
  }

  // Call CVodeSVtolerances to specify the scalar relative tolerance and vector absolute tol
  flag = CVodeSVtolerances(cvode_mem, reltol, abstol);
  if (check_flag(&flag, "CVodeSVtolerances", 1)) {
    return (1);
  }

  //--required in the new version ----------------------------------------------
  // Create dense SUNMatrix for use in linear solves
  // sunindextype y_len_M = y_len;
  int32_t mm = 3;
  SM = SUNDenseMatrix(mm, mm);
  if(check_flag((void *)SM, "SUNDenseMatrix", 0)) return (1);

  // Create dense SUNLinearSolver object for use by CVode
  LS = SUNDenseLinearSolver(y0, SM);
  if(check_flag((void *)LS, "SUNDenseLinearSolver", 0)) return(1);

  // Call CVDlsSetLinearSolver to attach the matrix and linear solver to CVode
  flag = CVDlsSetLinearSolver(cvode_mem, LS, SM);
  if(check_flag(&flag, "CVDlsSetLinearSolver", 1)) return(1);

  // NumericMatrix to store results - filled with 0.0
  // First row for initial conditions, First column is for time
  NumericMatrix soln = NumericMatrix(time_vec.length(), IC.length() + 1);

  // fill the first row of soln matrix with Initial Conditions
  soln(0,0) = time_vec[0];
  for(int i = 0; i<IC.length(); i++){
    soln(0,i+1) = IC[i];
  }

  // Call CVodeInit to initialize the integrator memory and specify the
  // user's right hand side function in y'=f(time,y),
  // // the inital time T0, and the initial dependent variable vector y.
  realtype tout;  // For output times
  for(int iout = 0; iout < NOUT-1; iout++) {

    // output times start from the index after initial time
    tout = time_vec[iout+1];

    flag = CVode(cvode_mem, tout, y0, &time, CV_NORMAL);

    if (check_flag(&flag, "CVode", 1)) { break; } // Something went wrong in solving it!
    if (flag == CV_SUCCESS) {

      // store results in soln matrix
      soln(iout+1, 0) = time;           // first column is for time
      for (int i = 0; i<IC.length(); i++){
        soln(iout+1, i+1) = y0_ptr[i];
      }
    }
  }

  // free the vectors
  N_VDestroy(y0);
  N_VDestroy(abstol);

  // free integrator memory
  CVodeFree(&cvode_mem);

  // free the linear solver memory
  SUNLinSolFree(LS);

  // Free the matrix memory
  SUNMatDestroy(SM);

  return soln;
  // return flag;

}

//--- CVODE definition ends ----------------------------------------------------




