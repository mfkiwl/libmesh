// The libMesh Finite Element Library.
// Copyright (C) 2002-2025 Benjamin S. Kirk, John W. Peterson, Roy H. Stogner

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


// C++ includes

// Local includes
#include "libmesh/fe.h"
#include "libmesh/elem.h"


namespace libMesh
{


LIBMESH_DEFAULT_VECTORIZED_FE(0,RATIONAL_BERNSTEIN)


template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape(const ElemType,
                                     const Order,
                                     const unsigned int libmesh_dbg_var(i),
                                     const Point &)
{
  libmesh_assert_less (i, 1);
  return 1.;
}



template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape(const Elem *,
                                     const Order,
                                     const unsigned int libmesh_dbg_var(i),
                                     const Point &,
                                     const bool)
{
  libmesh_assert_less (i, 1);
  return 1.;
}

template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape(const FEType,
                                     const Elem *,
                                     const unsigned int libmesh_dbg_var(i),
                                     const Point &,
                                     const bool)
{
  libmesh_assert_less (i, 1);
  return 1.;
}


template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape_deriv(const ElemType,
                                           const Order,
                                           const unsigned int,
                                           const unsigned int,
                                           const Point &)
{
  libmesh_error_msg("No spatial derivatives in 0D!");
  return 0.;
}



template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape_deriv(const Elem *,
                                           const Order,
                                           const unsigned int,
                                           const unsigned int,
                                           const Point &,
                                           const bool)
{
  libmesh_error_msg("No spatial derivatives in 0D!");
  return 0.;
}



template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape_deriv(const FEType,
                                           const Elem *,
                                           const unsigned int,
                                           const unsigned int,
                                           const Point &,
                                           const bool)
{
  libmesh_error_msg("No spatial derivatives in 0D!");
  return 0.;
}


#ifdef LIBMESH_ENABLE_SECOND_DERIVATIVES

template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape_second_deriv(const ElemType,
                                                  const Order,
                                                  const unsigned int,
                                                  const unsigned int,
                                                  const Point &)
{
  libmesh_error_msg("No spatial derivatives in 0D!");
  return 0.;
}



template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape_second_deriv(const Elem *,
                                                  const Order,
                                                  const unsigned int,
                                                  const unsigned int,
                                                  const Point &,
                                                  const bool)
{
  libmesh_error_msg("No spatial derivatives in 0D!");
  return 0.;
}



template <>
Real FE<0,RATIONAL_BERNSTEIN>::shape_second_deriv(const FEType,
                                                  const Elem *,
                                                  const unsigned int,
                                                  const unsigned int,
                                                  const Point &,
                                                  const bool)
{
  libmesh_error_msg("No spatial derivatives in 0D!");
  return 0.;
}


#endif

} // namespace libMesh
