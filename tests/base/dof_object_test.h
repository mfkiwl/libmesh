#ifndef __dof_object_test_h__
#define __dof_object_test_h__

#include "libmesh_cppunit.h"

#define DOFOBJECTTEST                           \
  CPPUNIT_TEST( testSetId );                    \
  CPPUNIT_TEST( testValidId );                  \
  CPPUNIT_TEST( testInvalidateId );             \
  CPPUNIT_TEST( testSetProcId );                \
  CPPUNIT_TEST( testValidProcId );              \
  CPPUNIT_TEST( testInvalidateProcId );         \
  CPPUNIT_TEST( testSetNSystems );              \
  CPPUNIT_TEST( testSetNVariableGroups );       \
  CPPUNIT_TEST( testAddExtraData );             \
  CPPUNIT_TEST( testAddSystemExtraInts );       \
  CPPUNIT_TEST( testSetNSystemsExtraInts );     \
  CPPUNIT_TEST( testSetNVariableGroupsExtraInts ); \
  CPPUNIT_TEST( testManualDofCalculation );     \
  CPPUNIT_TEST( testJensEftangBug );

using namespace libMesh;

template <class DerivedClass>
class DofObjectTest {

private:
  DerivedClass * instance;

protected:
  std::string libmesh_suite_name;

public:
  void setUp(DerivedClass * derived_instance)
  {
    instance=derived_instance;
  }

  void testSetId()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.set_id(1);
    CPPUNIT_ASSERT_EQUAL( static_cast<dof_id_type>(1) , aobject.id() );
  }

  void testValidId()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.set_id(1);
    CPPUNIT_ASSERT( aobject.valid_id() );

    aobject.set_id(DofObject::invalid_id);
    CPPUNIT_ASSERT( !aobject.valid_id() );
  }

  void testInvalidateId()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.set_id(1);
    aobject.invalidate_id();

    CPPUNIT_ASSERT( !aobject.valid_id() );
  }

  void testSetProcId()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.processor_id(libMesh::global_processor_id());
    CPPUNIT_ASSERT_EQUAL(static_cast<processor_id_type>(libMesh::global_processor_id()), aobject.processor_id());
  }

  void testValidProcId()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.processor_id(libMesh::global_processor_id());
    CPPUNIT_ASSERT(aobject.valid_processor_id());

    aobject.processor_id(DofObject::invalid_processor_id);
    CPPUNIT_ASSERT(!aobject.valid_processor_id());
  }

  void testInvalidateProcId()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.processor_id(libMesh::global_processor_id());
    aobject.invalidate_processor_id();

    CPPUNIT_ASSERT( !aobject.valid_processor_id() );
  }

  void testSetNSystems()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.set_n_systems (10);

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(10), aobject.n_systems());
  }

  void testSetNVariableGroups()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.set_n_systems (2);

    std::vector<unsigned int> nvpg;

    nvpg.push_back(10);
    nvpg.push_back(20);
    nvpg.push_back(30);

    aobject.set_n_vars_per_group (0, nvpg);
    aobject.set_n_vars_per_group (1, nvpg);

    for (unsigned int s=0; s<2; s++)
      {
        CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(60), aobject.n_vars(s));
        CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(3),  aobject.n_var_groups(s));

        for (unsigned int vg=0; vg<3; vg++)
          CPPUNIT_ASSERT_EQUAL( nvpg[vg], aobject.n_vars(s,vg) );
      }
  }

  void testAddExtraData()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.add_extra_integers (9);

    CPPUNIT_ASSERT(aobject.has_extra_integers());

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(9), aobject.n_extra_integers());

    unsigned int ints_per_Real = (sizeof(Real)-1)/sizeof(dof_id_type) + 1;

    for (unsigned int i=0; i != 9; ++i)
      CPPUNIT_ASSERT_EQUAL( DofObject::invalid_id, aobject.get_extra_integer(i) );

    for (unsigned int i=0; i != 9; ++i)
      {
        // Try out a char at i=1
        if (i == 1)
          aobject.set_extra_datum<char>(i, '1');
        // Try out an extra Real at i=2 if we'll have room
        if (i == 2 && ints_per_Real <= 4)
          aobject.set_extra_datum<Real>(i, pi);
        if (i < 1 || i >= (2 + ints_per_Real))
          {
            aobject.set_extra_integer(i, i);
            CPPUNIT_ASSERT_EQUAL( static_cast<dof_id_type>(i), aobject.get_extra_integer(i) );
          }
      }

    aobject.add_extra_integers (6);

    CPPUNIT_ASSERT(aobject.has_extra_integers());

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(6), aobject.n_extra_integers());

    for (unsigned int i=0; i != 6; ++i)
      {
        if (i == 1)
          CPPUNIT_ASSERT_EQUAL(aobject.get_extra_datum<char>(i), '1');
        if (i == 2 && ints_per_Real <= 4)
          CPPUNIT_ASSERT_EQUAL(aobject.get_extra_datum<Real>(i), pi);
        if (i < 1 || i >= (2 + ints_per_Real))
          CPPUNIT_ASSERT_EQUAL( static_cast<dof_id_type>(i), aobject.get_extra_integer(i) );
      }
  }

  void testAddSystemExtraInts()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.add_extra_integers (1);

    aobject.add_system();

    CPPUNIT_ASSERT(aobject.has_extra_integers());

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(1), aobject.n_extra_integers());
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(1), aobject.n_systems());
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(0), aobject.n_vars(0));

    aobject.add_extra_integers (4);

    aobject.add_system();

    CPPUNIT_ASSERT(aobject.has_extra_integers());

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(4), aobject.n_extra_integers());
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(2), aobject.n_systems());
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(0), aobject.n_vars(0));
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(0), aobject.n_vars(1));

    for (unsigned int i=0; i != 4; ++i)
      {
        CPPUNIT_ASSERT_EQUAL( DofObject::invalid_id, aobject.get_extra_integer(i) );
        aobject.set_extra_integer(i, i);
        CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));
      }

    aobject.add_extra_integers (7);

    for (unsigned int i=0; i != 4; ++i)
      CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));

    for (unsigned int i=4; i != 7; ++i)
      CPPUNIT_ASSERT_EQUAL( DofObject::invalid_id, aobject.get_extra_integer(i) );

    aobject.add_system();

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(7), aobject.n_extra_integers());

    for (unsigned int i=0; i != 4; ++i)
      CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));

    for (unsigned int i=4; i != 7; ++i)
      {
        CPPUNIT_ASSERT_EQUAL( DofObject::invalid_id, aobject.get_extra_integer(i) );
        aobject.set_extra_integer(i, i);
        CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));
      }

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(3), aobject.n_systems());
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(0), aobject.n_vars(0));
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(0), aobject.n_vars(1));
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(0), aobject.n_vars(2));

    for (unsigned int i=0; i != 7; ++i)
      CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));
  }

  void testSetNSystemsExtraInts()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.add_extra_integers (5);

    aobject.set_n_systems (10);

    CPPUNIT_ASSERT(aobject.has_extra_integers());

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(5), aobject.n_extra_integers());
    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(10), aobject.n_systems());

    for (unsigned int i=0; i != 5; ++i)
      {
        CPPUNIT_ASSERT_EQUAL( DofObject::invalid_id, aobject.get_extra_integer(i) );
        aobject.set_extra_integer(i, i);
        CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));
      }

    aobject.add_extra_integers (9);

    for (unsigned int i=0; i != 5; ++i)
      CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));

    for (unsigned int i=5; i != 9; ++i)
      CPPUNIT_ASSERT_EQUAL( DofObject::invalid_id, aobject.get_extra_integer(i) );

    aobject.set_n_systems (6);

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(9), aobject.n_extra_integers());

    for (unsigned int i=0; i != 5; ++i)
      CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));

    for (unsigned int i=5; i != 9; ++i)
      {
        CPPUNIT_ASSERT_EQUAL( DofObject::invalid_id, aobject.get_extra_integer(i) );
        aobject.set_extra_integer(i, i);
        CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));
      }

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(6), aobject.n_systems());

    for (unsigned int i=0; i != 9; ++i)
      CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));
  }

  void testSetNVariableGroupsExtraInts()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.set_n_systems (2);

    aobject.add_extra_integers (5);

    for (unsigned int i=0; i != 5; ++i)
      {
        CPPUNIT_ASSERT_EQUAL( DofObject::invalid_id, aobject.get_extra_integer(i) );
        aobject.set_extra_integer(i, i);
        CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));
      }

    std::vector<unsigned int> nvpg;

    nvpg.push_back(10);
    nvpg.push_back(20);
    nvpg.push_back(30);

    aobject.set_n_vars_per_group (0, nvpg);
    aobject.set_n_vars_per_group (1, nvpg);

    for (unsigned int s=0; s<2; s++)
      {
        CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(60), aobject.n_vars(s));
        CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(3),  aobject.n_var_groups(s));

        for (unsigned int vg=0; vg<3; vg++)
          CPPUNIT_ASSERT_EQUAL( nvpg[vg], aobject.n_vars(s,vg) );
      }

    CPPUNIT_ASSERT_EQUAL(static_cast<unsigned int>(5), aobject.n_extra_integers());

    for (unsigned int i=0; i != 5; ++i)
      CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(i), aobject.get_extra_integer(i));
  }


  void testManualDofCalculation()
  {
    LOG_UNIT_TEST;

    DofObject & aobject(*instance);

    aobject.set_n_systems (2);

    std::vector<unsigned int> nvpg;

    nvpg.push_back(2);
    nvpg.push_back(3);

    aobject.set_n_vars_per_group (0, nvpg);
    aobject.set_n_vars_per_group (1, nvpg);

    aobject.set_n_comp_group (0, 0, 1);
    aobject.set_n_comp_group (0, 1, 3);

    aobject.set_n_comp_group (1, 0, 2);
    aobject.set_n_comp_group (1, 1, 1);

    aobject.set_vg_dof_base(0, 0, 0);
    aobject.set_vg_dof_base(0, 1, 120);

    aobject.set_vg_dof_base(1, 0, 20);
    aobject.set_vg_dof_base(1, 1, 220);

    // Make sure the first dof is sane
    CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(0), aobject.dof_number(0, 0, 0));

    // Check that we can manually index dofs of variables based on the first dof in a variable group
    // Using: id = base + var_in_vg*ncomp + comp
    CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(aobject.vg_dof_base(0, 0) + 1*1 + 0), aobject.dof_number(0, 1, 0));

    // Another Check that we can manually index dofs of variables based on the first dof in a variable group
    // Using: id = base + var_in_vg*ncomp + comp
    CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(aobject.vg_dof_base(0, 1) + 2*3 + 2), aobject.dof_number(0, 4, 2));

    // One More Check that we can manually index dofs of variables based on the first dof in a variable group
    // Using: id = base + var_in_vg*ncomp + comp
    CPPUNIT_ASSERT_EQUAL(static_cast<dof_id_type>(aobject.vg_dof_base(1, 1) + 0*3 + 0), aobject.dof_number(1, 2, 0));
  }

  void testJensEftangBug()
  {
    LOG_UNIT_TEST;

    // For more information on this bug, see the following email thread:
    // https://sourceforge.net/p/libmesh/mailman/libmesh-users/thread/50C8EE7C.8090405@gmail.com/
    DofObject & aobject(*instance);
    dof_id_type buf0[] = {2, 8, 257, 0, 257, 96, 257, 192, 257, 0};
    aobject.set_buffer(std::vector<dof_id_type>(buf0, buf0+10));

    CPPUNIT_ASSERT_EQUAL (aobject.dof_number(0,0,0), static_cast<dof_id_type>(  0));
    CPPUNIT_ASSERT_EQUAL (aobject.dof_number(0,1,0), static_cast<dof_id_type>( 96));
    CPPUNIT_ASSERT_EQUAL (aobject.dof_number(0,2,0), static_cast<dof_id_type>(192));
    CPPUNIT_ASSERT_EQUAL (aobject.dof_number(1,0,0), static_cast<dof_id_type>(  0));

    dof_id_type buf1[] = {2, 8, 257, 1, 257, 97, 257, 193, 257, 1};
    aobject.set_buffer(std::vector<dof_id_type>(buf1, buf1+10));

    CPPUNIT_ASSERT_EQUAL (aobject.dof_number(0,0,0), static_cast<dof_id_type>(  1));
    CPPUNIT_ASSERT_EQUAL (aobject.dof_number(0,1,0), static_cast<dof_id_type>( 97));
    CPPUNIT_ASSERT_EQUAL (aobject.dof_number(0,2,0), static_cast<dof_id_type>(193));
    CPPUNIT_ASSERT_EQUAL (aobject.dof_number(1,0,0), static_cast<dof_id_type>(  1));
  }
};

#endif // #ifdef __dof_object_test_h__
