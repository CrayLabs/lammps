/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing author: Matthew Ellis (HPE)
------------------------------------------------------------------------- */

#include "dump_atom_smartsim.h"
#include "atom.h"
#include "domain.h"
#include "error.h"
#include "group.h"
#include "memory.h"
#include "universe.h"
#include "update.h"
#include <cstring>

using namespace LAMMPS_NS;

DumpAtomSmartSim::DumpAtomSmartSim(LAMMPS *lmp, int narg, char **arg)
    : DumpAtom(lmp, narg, arg)
{
  SmartRedis::Client* client = NULL;
  this->_client = NULL;
    try {
        Client* client = new Client(true);
        this->_client = client;
    }
    catch(std::exception& e) {
        throw std::runtime_error(e.what());
    }
    catch(...) {
        throw std::runtime_error("A non-standard exception "\
                                 "was encountered during SmartRedis client "\
                                 "construction.");
    }
}

/* ---------------------------------------------------------------------- */

DumpAtomSmartSim::~DumpAtomSmartSim()
{
  if(this->_client != NULL)
    delete this->_client;
}

/* ---------------------------------------------------------------------- */

void DumpAtomSmartSim::write()
{

  if (domain->triclinic == 0) {
    boxxlo = domain->boxlo[0];
    boxxhi = domain->boxhi[0];
    boxylo = domain->boxlo[1];
    boxyhi = domain->boxhi[1];
    boxzlo = domain->boxlo[2];
    boxzhi = domain->boxhi[2];
  } else {
    boxxlo = domain->boxlo_bound[0];
    boxxhi = domain->boxhi_bound[0];
    boxylo = domain->boxlo_bound[1];
    boxyhi = domain->boxhi_bound[1];
    boxzlo = domain->boxlo_bound[2];
    boxzhi = domain->boxhi_bound[2];
    boxxy = domain->xy;
    boxxz = domain->xz;
    boxyz = domain->yz;
  }


  /* Construct DataSet object with unique
  name based on user prefix, MPI rank, and
  timestep
  */
  SmartRedis::DataSet dataset(this->_make_dataset_key());

  /* Add a "domain" metadata field to the DataSet to hold information
  about the simulation domain.
  */
  dataset.add_meta_scalar("domain", &(domain->boxlo[0]), SRMetadataTypeDouble);
  dataset.add_meta_scalar("domain", &(domain->boxhi[0]), SRMetadataTypeDouble);
  dataset.add_meta_scalar("domain", &(domain->boxlo[1]), SRMetadataTypeDouble);
  dataset.add_meta_scalar("domain", &(domain->boxhi[1]), SRMetadataTypeDouble);
  dataset.add_meta_scalar("domain", &(domain->boxlo[2]), SRMetadataTypeDouble);
  dataset.add_meta_scalar("domain", &(domain->boxhi[2]), SRMetadataTypeDouble);

  /* Add a "triclinic" metadata field to the DataSet to indicate
  if the triclinic boolean is true in the simulation.
  */
  dataset.add_meta_scalar("triclinic", &(domain->triclinic), SRMetadataTypeInt64);

  /* If the triclinic boolean is true, add triclinic metadata
  fields to the DataSet.
  */
  if(domain->triclinic) {
    dataset.add_meta_scalar("triclinic_xy", &(domain->xy), SRMetadataTypeDouble);
    dataset.add_meta_scalar("triclinic_xz", &(domain->xz), SRMetadataTypeDouble);
    dataset.add_meta_scalar("triclinic_yz", &(domain->yz), SRMetadataTypeDouble);
  }

  /* Add a "scale_flag" metadata field ot the DataSet to indicate
  if the simulation scale_flag is true or false.
  */
  dataset.add_meta_scalar("scale_flag", &scale_flag, SRMetadataTypeInt64);

  /* Perform internal LAMMPS output preprocessing.
  */
  int n_local = atom->nlocal;
  int n_cols = (image_flag == 1) ? 8 : 5;

    nme = count();
    if (nme > maxbuf) {
        maxbuf = nme;
        memory->destroy(buf);
        memory->create(buf, (maxbuf * n_cols), "dump:buf");
    }
    if (sort_flag && sortcol == 0 && nme > maxids) {
        maxids = nme;
        memory->destroy(ids);
        memory->create(ids, maxids, "dump:ids");
    }

    if (sort_flag && sortcol == 0)
        pack(ids);
    else
        pack(NULL);
    if (sort_flag)
        sort();

    int64_t* data_int = new int64_t[n_local];
    double* data_dbl = new double[n_local];
    int buf_len = n_cols*n_local;

    std::vector<size_t> tensor_length;
    tensor_length.push_back(n_local);

    //Add atom ID tensor to the DataSet
    this->_pack_buf_into_array<int64_t>(data_int, buf_len, 0, n_cols);
    dataset.add_tensor("atom_id", data_int, tensor_length,
                       SRTensorTypeInt64, SRMemLayoutContiguous);

    //Add atom type tensor to the DataSet
    this->_pack_buf_into_array<int64_t>(data_int, buf_len, 1, n_cols);
    dataset.add_tensor("atom_type", data_int, tensor_length,
                       SRTensorTypeInt64, SRMemLayoutContiguous);

    //Add atom x position  tensor to the DataSet
    this->_pack_buf_into_array<double>(data_dbl, buf_len, 2, n_cols);
    dataset.add_tensor("atom_x", data_dbl, tensor_length,
                       SRTensorTypeDouble, SRMemLayoutContiguous);

    //Add atom y position  tensor to the DataSet
    this->_pack_buf_into_array<double>(data_dbl, buf_len, 3, n_cols);
    dataset.add_tensor("atom_y", data_dbl, tensor_length,
                       SRTensorTypeDouble, SRMemLayoutContiguous);

    //Add atom z position tensor to the DataSet
    this->_pack_buf_into_array<double>(data_dbl, buf_len, 4, n_cols);
    dataset.add_tensor("atom_z", data_dbl, tensor_length,
                       SRTensorTypeDouble, SRMemLayoutContiguous);

    /*Add "image_flag" metadata field to the DataSet to indicate
    if the image_flag boolean is true of false in the simulation.
    */
    dataset.add_meta_scalar("image_flag", &image_flag, SRMetadataTypeInt64);
    if (image_flag == 1) {
      //Add atom ix image tensor to the DataSet
      this->_pack_buf_into_array<int64_t>(data_int, buf_len, 5, n_cols);
      dataset.add_tensor("atom_ix", data_int, tensor_length,
                         SRTensorTypeInt64, SRMemLayoutContiguous);

      //Add atom iy image tensor to the DataSet
      this->_pack_buf_into_array<int64_t>(data_int, buf_len, 6, n_cols);
      dataset.add_tensor("atom_iy", data_int, tensor_length,
                         SRTensorTypeInt64, SRMemLayoutContiguous);

      //Add atom iz image tensor to the DataSet
      this->_pack_buf_into_array<int64_t>(data_int, buf_len, 7, n_cols);
      dataset.add_tensor("atom_iz", data_int, tensor_length,
                         SRTensorTypeInt64, SRMemLayoutContiguous);
    }

    /* Send the DataSet to the SmartSim experiment database
    */
    this->_client->put_dataset(dataset);

    /* Add the DataSet to an aggregation list for easier post-processing
    */
    this->_client->append_to_list(_make_list_key(), dataset);


    /* Free temporary memory needed to preprocess LAMMPS output
    */
    delete[] data_int;
    delete[] data_dbl;
}

/* ---------------------------------------------------------------------- */

void DumpAtomSmartSim::init_style()
{
    // setup function ptrs to use defaults from atom dump
    if (scale_flag == 1 && image_flag == 0 && domain->triclinic == 0)
        pack_choice = &DumpAtomSmartSim::pack_scale_noimage;
    else if (scale_flag == 1 && image_flag == 1 && domain->triclinic == 0)
        pack_choice = &DumpAtomSmartSim::pack_scale_image;
    else if (scale_flag == 1 && image_flag == 0 && domain->triclinic == 1)
        pack_choice = &DumpAtomSmartSim::pack_scale_noimage_triclinic;
    else if (scale_flag == 1 && image_flag == 1 && domain->triclinic == 1)
        pack_choice = &DumpAtomSmartSim::pack_scale_image_triclinic;
    else if (scale_flag == 0 && image_flag == 0)
        pack_choice = &DumpAtomSmartSim::pack_noscale_noimage;
    else if (scale_flag == 0 && image_flag == 1)
        pack_choice = &DumpAtomSmartSim::pack_noscale_image;
}

std::string DumpAtomSmartSim::_make_dataset_key()
{
  // create database key using the var_name

  int rank;
  MPI_Comm_rank(world, &rank);

  std::string prefix(filename);
  std::string key = prefix + "_rank_" + std::to_string(rank) +
    "_tstep_" + std::to_string(update->ntimestep);
  return key;
}

std::string DumpAtomSmartSim::_make_list_key()
{
  // create database key using the var_name

  return "tstep_" + std::to_string(update->ntimestep);
}

template <typename T>
void DumpAtomSmartSim::_pack_buf_into_array(T* data, int length,
					    int start_pos, int stride)
{
  // pack the dump buffer into an array to send to the database
  int c = 0;
  for(int i = start_pos; i < length; i+=stride) {
    data[c++] = buf[i];
  }
}
