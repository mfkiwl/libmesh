// The libMesh Finite Element Library.
// Copyright (C) 2002-2024 Benjamin S. Kirk, John W. Peterson, Roy H. Stogner

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



// Local includes
#include "libmesh/node.h"
#include "libmesh/elem.h"
#include "libmesh/reference_elem.h"
#include "libmesh/libmesh_singleton.h"
#include "libmesh/threads.h"
#include "libmesh/enum_to_string.h"
#include "libmesh/enum_elem_type.h"

// C++ includes
#include <map>
#include <sstream>



//-----------------------------------------------
// anonymous namespace for implementation details
namespace
{
using namespace libMesh;

namespace ElemDataStrings
{
// GCC 5.2.0 warns about overlength strings in the auto-generated
// reference_elem.data file.
#pragma GCC diagnostic ignored "-Woverlength-strings"
#include "reference_elem.data"
#pragma GCC diagnostic warning "-Woverlength-strings"
}

typedef Threads::spin_mutex InitMutex;

// Mutex for thread safety.
InitMutex init_mtx;

// map from ElemType to reference element file system object name
typedef std::map<ElemType, const char *> FileMapType;
FileMapType ref_elem_file;
Elem * ref_elem_map[INVALID_ELEM];



class SingletonCache : public libMesh::Singleton
{
public:
  ~SingletonCache()
  {
    for (auto & elem : elem_list)
      {
        delete elem;
        elem = nullptr;
      }

    elem_list.clear();

    for (auto & node : node_list)
      {
        delete node;
        node = nullptr;
      }

    node_list.clear();
  }

  std::vector<Node *> node_list;
  std::vector<Elem *> elem_list;
};

// singleton object, dynamically created and then
// removed at program exit
SingletonCache * singleton_cache = nullptr;



void read_ref_elem (const ElemType type_in,
                    std::istream & in)
{
  libmesh_assert (singleton_cache != nullptr);

  std::string dummy;
  unsigned int n_elem, n_nodes, elem_type_read, nn;
  double x, y, z;

  in >> dummy;
  in >> n_elem;  /**/ std::getline (in, dummy); libmesh_assert_equal_to (n_elem, 1);
  in >> n_nodes; /**/ std::getline (in, dummy);
  in >> dummy;   /**/ std::getline (in, dummy);
  in >> dummy;   /**/ std::getline (in, dummy);
  in >> dummy;   /**/ std::getline (in, dummy);
  in >> dummy;   /**/ std::getline (in, dummy);
  in >> n_elem;  /**/ std::getline (in, dummy); libmesh_assert_equal_to (n_elem, 1);

  in >> elem_type_read;

  libmesh_assert_less (elem_type_read, INVALID_ELEM);
  libmesh_assert_equal_to (elem_type_read, static_cast<unsigned int>(type_in));
  libmesh_assert_equal_to (n_nodes, Elem::type_to_n_nodes_map[elem_type_read]);

  // Construct elem of appropriate type
  std::unique_ptr<Elem> uelem = Elem::build(type_in);

  // We are expecting an identity map, so assert it!
  for (unsigned int n=0; n<n_nodes; n++)
    {
      in >> nn;
      libmesh_assert_equal_to (n,nn);
    }

  for (unsigned int n=0; n<n_nodes; n++)
    {
      in >> x >> y >> z;

      Node * node = new Node(x,y,z,n);
      singleton_cache->node_list.push_back(node);

      uelem->set_node(n) = node;
    }

  // it is entirely possible we ran out of file or encountered
  // another error.  If so, throw an error.
  libmesh_error_msg_if(!in, "ERROR while creating element singleton!");

  // Release the pointer into the care of the singleton_cache
  singleton_cache->elem_list.push_back (uelem.release());

  // Also store it in the array.
  ref_elem_map[type_in] = singleton_cache->elem_list.back();
}



void init_ref_elem_table()
{
  // outside mutex - if this pointer is set, we can trust it.
  if (singleton_cache != nullptr)
    return;

  // playing with fire here - lock before touching shared
  // data structures
  InitMutex::scoped_lock lock(init_mtx);

  // inside mutex - pointer may have changed while waiting
  // for the lock to acquire, check it again.
  if (singleton_cache != nullptr)
    return;

  // OK, if we get here we have the lock and we are not
  // initialized.  populate singleton.
  singleton_cache = new SingletonCache;

  // initialize the reference file table
  {
    ref_elem_file.clear();

    // 1D elements
    ref_elem_file[EDGE2]    = ElemDataStrings::one_edge;
    ref_elem_file[EDGE3]    = ElemDataStrings::one_edge3;
    ref_elem_file[EDGE4]    = ElemDataStrings::one_edge4;

    // 2D elements
    ref_elem_file[TRI3]     = ElemDataStrings::one_tri;
    ref_elem_file[TRI6]     = ElemDataStrings::one_tri6;
    ref_elem_file[TRI7]     = ElemDataStrings::one_tri7;

    ref_elem_file[QUAD4]    = ElemDataStrings::one_quad;
    ref_elem_file[QUAD8]    = ElemDataStrings::one_quad8;
    ref_elem_file[QUAD9]    = ElemDataStrings::one_quad9;

    // 3D elements
    ref_elem_file[HEX8]     = ElemDataStrings::one_hex;
    ref_elem_file[HEX20]    = ElemDataStrings::one_hex20;
    ref_elem_file[HEX27]    = ElemDataStrings::one_hex27;

    ref_elem_file[TET4]     = ElemDataStrings::one_tet;
    ref_elem_file[TET10]    = ElemDataStrings::one_tet10;
    ref_elem_file[TET14]    = ElemDataStrings::one_tet14;

    ref_elem_file[PRISM6]   = ElemDataStrings::one_prism;
    ref_elem_file[PRISM15]  = ElemDataStrings::one_prism15;
    ref_elem_file[PRISM18]  = ElemDataStrings::one_prism18;
    ref_elem_file[PRISM20]  = ElemDataStrings::one_prism20;
    ref_elem_file[PRISM21]  = ElemDataStrings::one_prism21;

    ref_elem_file[PYRAMID5] = ElemDataStrings::one_pyramid;
    ref_elem_file[PYRAMID13] = ElemDataStrings::one_pyramid13;
    ref_elem_file[PYRAMID14] = ElemDataStrings::one_pyramid14;
    ref_elem_file[PYRAMID18] = ElemDataStrings::one_pyramid18;
  }

  // Read'em
  for (const auto & [elem_type, filename] : ref_elem_file)
    {
      std::istringstream stream(filename);
      read_ref_elem(elem_type, stream);
    }
}


// no reason to do this at startup -
// data structures will get initialized *if*
// ReferenceElem::get() is ever called.
// // Class to setup singleton data
// class ReferenceElemSetup : public Singleton::Setup
// {
//   void setup ()
//   {
//     init_ref_elem_table();
//   }
// } reference_elem_setup;

} // anonymous namespace



//----------------------------------------------------------------------------
// external API Implementation
namespace libMesh
{
namespace ReferenceElem
{
const Elem & get (const ElemType type_in)
{
  ElemType base_type = type_in;

  // For shell elements, use non shell type as the base type
  if (type_in == TRISHELL3)
    base_type = TRI3;

  if (type_in == QUADSHELL4)
    base_type = QUAD4;

  if (type_in == QUADSHELL8)
    base_type = QUAD8;

  init_ref_elem_table();

  // Throw an error if the user asked for an ElemType that we don't
  // have a reference element for.
  libmesh_error_msg_if(ref_elem_map[base_type] == nullptr || type_in == INVALID_ELEM,
                       "No reference elem data available for ElemType " << type_in
                       << " = " << Utility::enum_to_string(type_in) << ".");

  return *ref_elem_map[base_type];
}
} // namespace ReferenceElem
} // namespace libMesh
