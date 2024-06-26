// Basic include files
#include "libmesh/equation_systems.h"
#include "libmesh/exodusII_io.h"
#include "libmesh/nemesis_io.h"
#include "libmesh/mesh.h"
#include "libmesh/mesh_generation.h"
#include "libmesh/parallel.h" // set_union
#include "libmesh/replicated_mesh.h"
#include "libmesh/string_to_enum.h"
#include "libmesh/boundary_info.h"
#include "libmesh/utility.h" // libmesh_map_find
#include "libmesh/elem.h"

#include "test_comm.h"
#include "libmesh_cppunit.h"


// Bring in everything from the libMesh namespace
using namespace libMesh;

class WriteElemsetData : public CppUnit::TestCase
{
public:
  LIBMESH_CPPUNIT_TEST_SUITE(WriteElemsetData);

#if LIBMESH_DIM > 1
#ifdef LIBMESH_HAVE_EXODUS_API
  CPPUNIT_TEST(testWriteExodus);
#endif // #ifdef LIBMESH_HAVE_EXODUS_API
#ifdef LIBMESH_HAVE_NEMESIS_API
  // CPPUNIT_TEST(testWriteNemesis); // Not yet implemented
#endif
#endif

  CPPUNIT_TEST_SUITE_END();

  void checkByCentroid(const PointLocatorBase & pl,
                       const Point & centroid,
                       unsigned int elemset_index,
                       dof_id_type expected_elemset_code)
  {
    const Elem * elem = pl(centroid);

    // For ReplicatedMesh, this Elem should be found on all procs, but
    // in case this test is ever run with a DistributedMesh, the Elem
    // won't be found on all procs, so we only test it on procs where
    // it is found.
    if (elem)
      CPPUNIT_ASSERT_EQUAL(/*expected=*/expected_elemset_code, /*actual=*/elem->get_extra_integer(elemset_index));
  }

  void checkElemsetCodes(const MeshBase & mesh)
  {
    // Make sure that the mesh actually has an extra_integer for "elemset_code"
    CPPUNIT_ASSERT(mesh.has_elem_integer("elemset_code"));

    // Check that the elements in mesh are in the correct elemsets.
    // The elemset_codes will not in general match because they are
    // created by a generic algorithm in the Exodus reader while above
    // they were hard-coded.
    unsigned int elemset_index = mesh.get_elem_integer_index("elemset_code");

    // Make sure the elemset_codes match what we are expecting.
    // The Exodus reader assigns the codes based on operator<
    // for std::sets, which gives us the ordering {1}, {1,2}, {2}
    CPPUNIT_ASSERT_EQUAL(/*expected=*/static_cast<dof_id_type>(0), /*actual=*/mesh.get_elemset_code({1}));
    CPPUNIT_ASSERT_EQUAL(/*expected=*/static_cast<dof_id_type>(1), /*actual=*/mesh.get_elemset_code({1,2}));
    CPPUNIT_ASSERT_EQUAL(/*expected=*/static_cast<dof_id_type>(2), /*actual=*/mesh.get_elemset_code({2}));

    // Debugging: print vertex_average() for some elements. Perhaps we can use this to uniquely identify
    // elements for the test...
    // for (auto id : {8,14,3,24,9,15})
    //   libMesh::out << "Elem " << id
    //                << ", vertex average = " << mesh.elem_ptr(id)->vertex_average()
    //                << std::endl;

    // We'll use a PointLocator to quickly find elements by centroid
    auto pl = mesh.sub_point_locator();

    // Return nullptr when Points are not located in any element
    // rather than crashing. When running in parallel, this happens
    // quite often.
    pl->enable_out_of_mesh_mode();

    // Test that elements have the same elemset codes they did prior to being written to file.
    checkByCentroid(*pl, Point(0.4, -0.4, 0), elemset_index, /*expected_elemset_code=*/0); // original Elem 8
    checkByCentroid(*pl, Point(0.8, 0, 0),    elemset_index, /*expected_elemset_code=*/0); // original Elem 14
    checkByCentroid(*pl, Point(0.4, -0.8, 0), elemset_index, /*expected_elemset_code=*/1); // original Elem 3
    checkByCentroid(*pl, Point(0.8, 0.8, 0),  elemset_index, /*expected_elemset_code=*/1); // original Elem 24
    checkByCentroid(*pl, Point(0.8, -0.4, 0), elemset_index, /*expected_elemset_code=*/2); // original Elem 9
    checkByCentroid(*pl, Point(-0.8, 0.4, 0), elemset_index, /*expected_elemset_code=*/2); // original Elem 15
  }

  template <typename IOClass>
  void testWriteImpl(const std::string & filename)
  {
    // TODO: Currently this test only works for ReplicatedMesh. It
    // should be updated so that it works for DistributedMesh as well,
    // and then we can just set MeshType == Mesh.
    typedef ReplicatedMesh MeshType;

    // Construct mesh for writing
    MeshType mesh(*TestCommWorld);

    // Allocate space for an extra integer on each element to store a "code" which
    // determines which sets an Elem belongs to. We do this before building the Mesh.
    //
    // extra_integer val               & sets elem belongs to
    // DofObject::invalid_id (default) & elem belongs to no sets
    // 1                               & elem belongs to set A only
    // 2                               & elem belongs to set B only
    // 3                               & elem belongs to sets A and B
    unsigned int elemset_index =
      mesh.add_elem_integer("elemset_code",
                            /*allocate_data=*/true);

    // We are generating this mesh, so it should not be renumbered.
    // No harm in being explicit about it, however.
    mesh.allow_renumbering(false);

    MeshTools::Generation::build_square(mesh,
                                        /*nx=*/5, /*ny=*/5,
                                        -1., 1.,
                                        -1., 1.,
                                        QUAD4);

    // Set ids for elements in elemsets 1, 2
    std::set<dof_id_type> set1 = {3, 8, 14, 24};
    std::set<dof_id_type> set2 = {3, 9, 15, 24};

    // Loop over Elems and set "elemset_code" values
    for (const auto & elem : mesh.element_ptr_range())
      {
        bool
          in1 = set1.count(elem->id()),
          in2 = set2.count(elem->id());

        dof_id_type val = DofObject::invalid_id;
        if (in1)
          val = 1;
        if (in2)
          val = 2;
        if (in1 && in2)
          val = 3;

        elem->set_extra_integer(elemset_index, val);
      }

    // Tell the Mesh about these elemsets
    mesh.add_elemset_code(1, {1});
    mesh.add_elemset_code(2, {2});
    mesh.add_elemset_code(3, {1,2});

    // Debugging: print valid elemset_codes values
    // for (const auto & elem : mesh.element_ptr_range())
    //   {
    //     dof_id_type elemset_code =
    //       elem->get_extra_integer(elemset_index);
    //
    //     if (elemset_code != DofObject::invalid_id)
    //       libMesh::out << "Elem " << elem->id() << ", elemset_code = " << elemset_code << std::endl;
    //   }

    // Set up variables defined on these elemsets
    std::vector<std::string> var_names = {"var1", "var2", "var3"};
    std::vector<std::set<elemset_id_type>> elemset_ids =
      {
        {1},  // var1 is defined on elemset 1
        {2},  // var2 is defined on elemset 2
        {1,2} // var3 is defined on elemsets 1 and 2
      };
    std::vector<std::map<std::pair<dof_id_type, elemset_id_type>, Real>> elemset_vals(var_names.size());

    // To catch values handed back by MeshBase::get_elemsets()
    std::set<elemset_id_type> id_set_to_fill;

    for (const auto & elem : mesh.element_ptr_range())
      {
        // Get list of elemset ids to which this element belongs
        mesh.get_elemsets(elem->get_extra_integer(elemset_index), id_set_to_fill);

        bool
          in1 = id_set_to_fill.count(1),
          in2 = id_set_to_fill.count(2);

        // Set the value for var1 == 1.0 on all elements in elemset 1
        if (in1)
          elemset_vals[/*var1 index=*/0].emplace( std::make_pair(elem->id(), /*elemset_id=*/1), 1.0);

        // Set the value of var2 == 2.0 on all elements in elemset 2
        if (in2)
          elemset_vals[/*var2 index=*/1].emplace( std::make_pair(elem->id(), /*elemset_id=*/2), 2.0);

        // Set the value of var3 == 3.0 on elements in the union of sets 1 and 2
        if (in1 || in2)
          for (const auto & id : id_set_to_fill)
            elemset_vals[/*var3 index=*/2].emplace( std::make_pair(elem->id(), /*elemset_id=*/id), 3.0);
      }

    // Sanity check: we should have 8 total elements in set1 and set2 combined
    CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(8), elemset_vals[/*var3 index=*/2].size());

    // Lambda to help with debugging
    // auto print_map = [](const std::vector<std::map<std::pair<dof_id_type, elemset_id_type>, Real>> & input)
    //   {
    //     for (auto i : index_range(input))
    //       {
    //         libMesh::out << "Map " << i << " = " << std::endl;
    //         for (const auto & [pr, val] : input[i])
    //           {
    //             const auto & elem_id = pr.first;
    //             const auto & elemset_id = pr.second;
    //             libMesh::out << "(" << elem_id << ", " << elemset_id << ") = " << val << std::endl;
    //           }
    //       }
    //   };

    // Debugging: print the elemset_vals struct we just built
    // print_map(elemset_vals);

    // Write the file in the ExodusII format, including the element set information.
    // Note: elemsets should eventually be written during ExodusII_IO::write(), this
    // would match the behavior of sidesets and nodesets.
    {
      IOClass writer(mesh);
      writer.write(filename);
      writer.write_elemset_data(/*timestep=*/1, var_names, elemset_ids, elemset_vals);
    }

    // Make sure that the writing is done before the reading starts.
    TestCommWorld->barrier();

    // Now read it back in
    MeshType read_mesh(*TestCommWorld);

    // Do not allow renumbering on this mesh either.
    read_mesh.allow_renumbering(false);

    IOClass reader(read_mesh);
    // reader.verbose(true); // additional messages while debugging
    reader.read(filename);

    // When reading in a Mesh using an "IOClass" object, it is not
    // automatically prepared for use, so do that now.
    read_mesh.prepare_for_use();

    // Do generic checks that are independent of IOClass
    checkElemsetCodes(read_mesh);

    // Read in the elemset variables from file. This is currently a
    // feature that is only supported by the Exodus IOClass, so it is
    // not part of the checkElemsetCodes() function.
    std::vector<std::string> read_in_var_names;
    std::vector<std::set<elemset_id_type>> read_in_elemset_ids;
    std::vector<std::map<std::pair<dof_id_type, elemset_id_type>, Real>> read_in_elemset_vals;
    reader.read_elemset_data(/*timestep=*/1, read_in_var_names, read_in_elemset_ids, read_in_elemset_vals);

    // Debugging
    // print_map(read_in_elemset_vals);

    // Assert that the data we read in matches what we wrote out
    CPPUNIT_ASSERT(read_in_var_names == var_names);
    CPPUNIT_ASSERT(read_in_elemset_ids == elemset_ids);
    CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(8), read_in_elemset_vals[/*var3 index=*/2].size());
    CPPUNIT_ASSERT(read_in_elemset_vals == elemset_vals);

    // Also check that the flat indices match those in the file
    std::map<std::pair<dof_id_type, elemset_id_type>, unsigned int> elemset_array_indices;
    reader.get_elemset_data_indices(elemset_array_indices);

    // Verify that we have the following (Exodus-based) elem ids in the following indices.
    // These indices were copied from an ncdump of the exo file.
    std::vector<dof_id_type> elem_els1 = {4, 9, 15, 25};
    std::vector<dof_id_type> elem_els2 = {4, 10, 16, 25};

    for (auto i : index_range(elem_els1))
      CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(i),
                           elemset_array_indices[std::make_pair(/*elem id=*/elem_els1[i] - 1, // convert to libmesh id
                                                                /*set id*/1)]);
    for (auto i : index_range(elem_els2))
      CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(i),
                           elemset_array_indices[std::make_pair(/*elem id=*/elem_els2[i] - 1, // convert to libmesh id
                                                                /*set id*/2)]);

#ifdef LIBMESH_HAVE_XDR
    // Also test that we can successfully write elemset codes to
    // XDR/XDA files. Only do this if XDR is enabled. In theory, we
    // could still test that the ASCII (xda) file writing capability
    // still works even when the binary (xdr) file writing capability
    // is disabled; in practice this is probably not worth the extra
    // hassle.

    // Now write an xda file so that we can test that elemset codes
    // are preserved when reading the Mesh back in.
    read_mesh.write("write_elemset_data.xda");

    // Make sure that the writing is done before the reading starts.
    TestCommWorld->barrier();

    // Now read it back in and do generic checks that are independent of IOClass.
    Mesh read_mesh2(*TestCommWorld);
    // XDR files implicitly renumber mesh files in parallel, so setting this flag
    // does not have the desired effect of preventing renumbering in that case.
    read_mesh2.allow_renumbering(false);
    read_mesh2.read("write_elemset_data.xda");
    checkElemsetCodes(read_mesh2);

#endif // LIBMESH_HAVE_XDR
  }

  void testWriteExodus()
  {
    LOG_UNIT_TEST;

    testWriteImpl<ExodusII_IO>("write_elemset_data.e");
  }

  void testWriteNemesis()
  {
    // LOG_UNIT_TEST;

    // FIXME: Not yet implemented
    // testWriteImpl<Nemesis_IO>("write_elemset_data.n");
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(WriteElemsetData);
