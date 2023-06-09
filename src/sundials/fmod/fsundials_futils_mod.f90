! This file was automatically generated by SWIG (http://www.swig.org).
! Version 4.0.0
!
! Do not make changes to this file unless you know what you are doing--modify
! the SWIG interface file instead.

! ---------------------------------------------------------------
! Programmer(s): Auto-generated by swig.
! ---------------------------------------------------------------
! SUNDIALS Copyright Start
! Copyright (c) 2002-2023, Lawrence Livermore National Security
! and Southern Methodist University.
! All rights reserved.
!
! See the top-level LICENSE and NOTICE files for details.
!
! SPDX-License-Identifier: BSD-3-Clause
! SUNDIALS Copyright End
! ---------------------------------------------------------------

module fsundials_futils_mod
 use, intrinsic :: ISO_C_BINDING
 implicit none
 private

 ! DECLARATION CONSTRUCTS
 type, bind(C) :: SwigArrayWrapper
  type(C_PTR), public :: data = C_NULL_PTR
  integer(C_SIZE_T), public :: size = 0
 end type
 public :: FSUNDIALSFileOpen
 public :: FSUNDIALSFileClose

! WRAPPER DECLARATIONS
interface
function swigc_FSUNDIALSFileOpen(farg1, farg2) &
bind(C, name="_wrap_FSUNDIALSFileOpen") &
result(fresult)
use, intrinsic :: ISO_C_BINDING
import :: swigarraywrapper
type(SwigArrayWrapper) :: farg1
type(SwigArrayWrapper) :: farg2
type(C_PTR) :: fresult
end function

subroutine swigc_FSUNDIALSFileClose(farg1) &
bind(C, name="_wrap_FSUNDIALSFileClose")
use, intrinsic :: ISO_C_BINDING
type(C_PTR), value :: farg1
end subroutine

end interface


contains
 ! MODULE SUBPROGRAMS

subroutine SWIG_string_to_chararray(string, chars, wrap)
  use, intrinsic :: ISO_C_BINDING
  character(kind=C_CHAR, len=*), intent(IN) :: string
  character(kind=C_CHAR), dimension(:), target, allocatable, intent(OUT) :: chars
  type(SwigArrayWrapper), intent(OUT) :: wrap
  integer :: i

  allocate(character(kind=C_CHAR) :: chars(len(string) + 1))
  do i=1,len(string)
    chars(i) = string(i:i)
  end do
  i = len(string) + 1
  chars(i) = C_NULL_CHAR ! C string compatibility
  wrap%data = c_loc(chars)
  wrap%size = len(string)
end subroutine

function FSUNDIALSFileOpen(filename, modes) &
result(swig_result)
use, intrinsic :: ISO_C_BINDING
type(C_PTR) :: swig_result
character(kind=C_CHAR, len=*), target :: filename
character(kind=C_CHAR), dimension(:), allocatable, target :: farg1_chars
character(kind=C_CHAR, len=*), target :: modes
character(kind=C_CHAR), dimension(:), allocatable, target :: farg2_chars
type(C_PTR) :: fresult 
type(SwigArrayWrapper) :: farg1 
type(SwigArrayWrapper) :: farg2 

call SWIG_string_to_chararray(filename, farg1_chars, farg1)
call SWIG_string_to_chararray(modes, farg2_chars, farg2)
fresult = swigc_FSUNDIALSFileOpen(farg1, farg2)
swig_result = fresult
end function

subroutine FSUNDIALSFileClose(fp)
use, intrinsic :: ISO_C_BINDING
type(C_PTR) :: fp
type(C_PTR) :: farg1 

farg1 = fp
call swigc_FSUNDIALSFileClose(farg1)
end subroutine


end module
