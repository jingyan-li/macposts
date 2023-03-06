// XXX: On macOS with the stock Clang compiler, including 'pybind11/eigen.h'
// after 'Snap.h' will cause a nasty compilation error because Snap defines an
// overloaded '!=' operator and the compiler is confused. So here is a quick
// and dirty fix: include 'pybind11/eigen.h' first. However, we probably need
// to find a real solution.

#include <pybind11/eigen.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <set>
#include <unordered_map>
#include <vector>

#include <Snap.h>
#include <dta.h>
#include <dta_gradient_utls.h>
#include <multiclass.h>
#include <ults.h>

namespace py = pybind11;
using SparseMatrixR = Eigen::SparseMatrix<double, Eigen::RowMajor>;

// Single class DTA

class Dta
{
public:
  Dta ();
  ~Dta ();
  int initialize (std::string folder);
  int install_cc ();
  int install_cc_tree ();
  int run_once ();
  int run_whole ();
  int register_links (py::array_t<int> links);
  // FIXME: This returns a Numpy array for consistency, but it should really be
  // better to use a plain list.
  py::array_t<int> get_registered_links ();
  int register_paths (py::array_t<int> paths);
  int get_cur_loading_interval ();
  py::array_t<double> get_link_inflow (py::array_t<int> start_intervals,
                                       py::array_t<int> end_intervals);
  py::array_t<double> get_link_tt (py::array_t<int> start_intervals);
  py::array_t<double> get_path_tt (py::array_t<int> start_intervals);
  py::array_t<double> get_link_in_cc (int link_ID);
  py::array_t<double> get_link_out_cc (int link_ID);
  py::array_t<double> get_dar_matrix (py::array_t<int> link_start_intervals,
                                      py::array_t<int> link_end_intervals);
  SparseMatrixR get_complete_dar_matrix (py::array_t<int> start_intervals,
                                         py::array_t<int> end_intervals,
                                         int num_intervals,
                                         py::array_t<double> f);
  MNM_Dta *m_dta;
  std::vector<MNM_Dlink *> m_link_vec;
  std::vector<MNM_Path *> m_path_vec;
  std::unordered_map<MNM_Path *, int> m_path_map;
  // std::unordered_map<MNM_Dlink*, int> m_link_map;
  std::unordered_map<TInt, MNM_Path *> m_ID_path_mapping;
};

Dta::Dta ()
{
  m_dta = NULL;
  m_link_vec = std::vector<MNM_Dlink *> ();
  m_path_vec = std::vector<MNM_Path *> ();
  m_path_map = std::unordered_map<MNM_Path *, int> ();
  m_ID_path_mapping = std::unordered_map<TInt, MNM_Path *> ();
  // m_link_map = std::unordered_map<MNM_Dlink*, int>();
}

Dta::~Dta ()
{
  if (m_dta != NULL)
    {
      delete m_dta;
    }
  m_link_vec.clear ();
  m_path_vec.clear ();
  // m_link_map.clear();
  m_ID_path_mapping.clear ();
}

int
Dta::initialize (std::string folder)
{
  m_dta = new MNM_Dta (folder);
  m_dta->build_from_files ();
  m_dta->hook_up_node_and_link ();
  // m_dta -> is_ok();
  // printf("start load ID path mapping 0\n");
  if (MNM_Routing_Fixed *_routing
      = dynamic_cast<MNM_Routing_Fixed *> (m_dta->m_routing))
    {
      MNM::get_ID_path_mapping (m_ID_path_mapping, _routing->m_path_table);
      return 0;
    }
  if (MNM_Routing_Hybrid *_routing
      = dynamic_cast<MNM_Routing_Hybrid *> (m_dta->m_routing))
    {
      // printf("start load ID path mapping\n");
      MNM::get_ID_path_mapping (m_ID_path_mapping,
                                _routing->m_routing_fixed->m_path_table);
      // printf("mapping size %d\n", m_ID_path_mapping.size());
      return 0;
    }
  // TODO: Check this.
  std::runtime_error ("Dta:: Routing type not implemented in API");
  return -1;
}

int
Dta::run_once ()
{
  return 0;
}

int
Dta::install_cc ()
{
  for (size_t i = 0; i < m_link_vec.size (); ++i)
    {
      m_link_vec[i]->install_cumulative_curve ();
    }
  return 0;
}

int
Dta::install_cc_tree ()
{
  for (size_t i = 0; i < m_link_vec.size (); ++i)
    {
      m_link_vec[i]->install_cumulative_curve_tree ();
    }
  return 0;
}

int
Dta::run_whole ()
{
  m_dta->pre_loading ();
  m_dta->loading (false);
  return 0;
}

int
Dta::get_cur_loading_interval ()
{
  return m_dta->m_current_loading_interval ();
}

int
Dta::register_links (py::array_t<int> links)
{
  if (m_link_vec.size () > 0)
    {
      printf ("Warning, Dta:register_links: link exists\n");
      m_link_vec.clear ();
    }
  auto links_buf = links.request ();
  if (links_buf.ndim != 1)
    {
      throw std::runtime_error ("Number of dimensions must be one");
    }
  int *links_ptr = (int *) links_buf.ptr;
  MNM_Dlink *_link;
  for (int i = 0; i < links_buf.shape[0]; i++)
    {
      _link = m_dta->m_link_factory->get_link (TInt (links_ptr[i]));
      if (std::find (m_link_vec.begin (), m_link_vec.end (), _link)
          != m_link_vec.end ())
        {
          throw std::runtime_error ("Error, Dta::register_links, link exists");
        }
      else
        {
          m_link_vec.push_back (_link);
          // m_link_map.insert(std::make_pair(_link, i));
        }
    }
  return 0;
}

py::array_t<int>
Dta::get_registered_links ()
{
  auto results = py::array_t<int> (m_link_vec.size ());
  auto results_buf = results.request ();
  int *results_ptr = static_cast<int *> (results_buf.ptr);
  for (int idx = 0; idx < m_link_vec.size (); idx++)
    results_ptr[idx] = (int) m_link_vec[idx]->m_link_ID;
  return results;
}

int
Dta::register_paths (py::array_t<int> paths)
{
  if (m_path_vec.size () > 0)
    {
      printf ("Warning, Dta::register_paths, path exists\n");
      m_path_vec.clear ();
      m_path_map.clear ();
    }
  auto paths_buf = paths.request ();
  if (paths_buf.ndim != 1)
    {
      throw std::runtime_error (
        "register_paths: Number of dimensions must be one");
    }
  int *paths_ptr = (int *) paths_buf.ptr;
  TInt _path_ID;
  for (int i = 0; i < paths_buf.shape[0]; i++)
    {
      _path_ID = TInt (paths_ptr[i]);
      // printf("registering path %d, %d\n", _path_ID(),
      // (int)m_ID_path_mapping.size());
      if (m_ID_path_mapping.find (_path_ID) == m_ID_path_mapping.end ())
        {
          throw std::runtime_error ("register_paths: No such path");
        }
      else
        {
          m_path_vec.push_back (m_ID_path_mapping[_path_ID]);
          m_path_map.insert (std::make_pair (m_ID_path_mapping[_path_ID], i));
        }
    }
  // m_path_set = std::set<MNM_Path*> (m_path_vec.begin(), m_path_vec.end());
  return 0;
}

py::array_t<double>
Dta::get_link_inflow (py::array_t<int> start_intervals,
                      py::array_t<int> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Dta::get_link_inflow, input dismension mismatch");
    }
  if (start_buf.shape[0] != end_buf.shape[0])
    {
      throw std::runtime_error (
        "Error, Dta::get_link_inflow, input length mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          if (end_ptr[t] < start_ptr[t])
            {
              throw std::runtime_error ("Error, Dta::get_link_inflow, end "
                                        "time smaller than start time");
            }
          if (end_ptr[t] > get_cur_loading_interval ())
            {
              throw std::runtime_error (
                "Error, Dta::get_link_inflow, loaded data not enough");
            }

          result_ptr[i * l + t]
            = MNM_DTA_GRADIENT::get_link_inflow (m_link_vec[i],
                                                 TFlt (start_ptr[t]),
                                                 TFlt (end_ptr[t])) ();
          // printf("i %d, t %d, %f\n", i, t, result_ptr[i * l + t]);
        }
    }
  return result;
}

py::array_t<double>
Dta::get_link_tt (py::array_t<int> start_intervals)
{
  auto start_buf = start_intervals.request ();
  if (start_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Dta::get_link_tt, input dismension mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };

  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  int *start_ptr = (int *) start_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          if (start_ptr[t] > get_cur_loading_interval ())
            {
              throw std::runtime_error (
                "Error, Dta::get_link_tt, loaded data not enough");
            }
          result_ptr[i * l + t]
            = MNM_DTA_GRADIENT::get_travel_time (m_link_vec[i],
                                                 TFlt (start_ptr[t])) ();
        }
    }
  return result;
}

py::array_t<double>
Dta::get_path_tt (py::array_t<int> start_intervals)
{
  auto start_buf = start_intervals.request ();
  if (start_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Dta::get_path_tt, input dismension mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_path_vec.size (), l };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  int *start_ptr = (int *) start_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (start_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error (
            "Error, Dta::get_path_tt, loaded data not enough");
        }
      for (size_t i = 0; i < m_path_vec.size (); ++i)
        {
          result_ptr[i * l + t]
            = MNM_DTA_GRADIENT::get_path_travel_time (m_path_vec[i],
                                                      TFlt (start_ptr[t]),
                                                      m_dta->m_link_factory) ();
        }
    }
  return result;
}

py::array_t<double>
Dta::get_link_in_cc (int link_ID)
{
  if (m_dta->m_link_factory->get_link (TInt (link_ID))->m_N_in == NULL)
    {
      throw std::runtime_error ("Error, Dta::get_link_in_cc, cc not installed");
    }
  std::deque<std::pair<TFlt, TFlt>> _record
    = m_dta->m_link_factory->get_link (TInt (link_ID))->m_N_in->m_recorder;
  int new_shape[2] = { (int) _record.size (), 2 };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  for (size_t i = 0; i < _record.size (); ++i)
    {
      result_ptr[i * 2] = _record[i].first ();
      result_ptr[i * 2 + 1] = _record[i].second ();
    }
  return result;
}

py::array_t<double>
Dta::get_link_out_cc (int link_ID)
{
  if (m_dta->m_link_factory->get_link (TInt (link_ID))->m_N_out == NULL)
    {
      throw std::runtime_error (
        "Error, Dta::get_link_out_cc, cc not installed");
    }
  std::deque<std::pair<TFlt, TFlt>> _record
    = m_dta->m_link_factory->get_link (TInt (link_ID))->m_N_out->m_recorder;
  int new_shape[2] = { (int) _record.size (), 2 };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  for (size_t i = 0; i < _record.size (); ++i)
    {
      result_ptr[i * 2] = _record[i].first ();
      result_ptr[i * 2 + 1] = _record[i].second ();
    }
  return result;
}

py::array_t<double>
Dta::get_dar_matrix (py::array_t<int> start_intervals,
                     py::array_t<int> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Dta::get_link_inflow, input dismension mismatch");
    }
  if (start_buf.shape[0] != end_buf.shape[0])
    {
      throw std::runtime_error (
        "Error, Dta::get_link_inflow, input length mismatch");
    }
  int l = start_buf.shape[0];
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  std::vector<dar_record *> _record = std::vector<dar_record *> ();
  // for (size_t i = 0; i<m_link_vec.size(); ++i){
  //   m_link_vec[i] -> m_N_in_tree -> print_out();
  // }
  for (int t = 0; t < l; ++t)
    {
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          if (end_ptr[t] < start_ptr[t])
            {
              throw std::runtime_error ("Error, Dta::get_dar_matrix, end time "
                                        "smaller than start time");
            }
          if (end_ptr[t] > get_cur_loading_interval ())
            {
              throw std::runtime_error (
                "Error, Dta::get_dar_matrix, loaded data not enough");
            }
          MNM_DTA_GRADIENT::add_dar_records (_record, m_link_vec[i], m_path_map,
                                             TFlt (start_ptr[t]),
                                             TFlt (end_ptr[t]));
        }
    }
  // path_ID, assign_time, link_ID, start_int, flow
  int new_shape[2] = { (int) _record.size (), 5 };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  dar_record *tmp_record;
  for (size_t i = 0; i < _record.size (); ++i)
    {
      tmp_record = _record[i];
      result_ptr[i * 5 + 0] = (double) tmp_record->path_ID ();
      result_ptr[i * 5 + 1] = (double) tmp_record->assign_int ();
      result_ptr[i * 5 + 2] = (double) tmp_record->link_ID ();
      result_ptr[i * 5 + 3] = (double) tmp_record->link_start_int ();
      result_ptr[i * 5 + 4] = tmp_record->flow ();
    }
  for (size_t i = 0; i < _record.size (); ++i)
    {
      delete _record[i];
    }
  _record.clear ();
  return result;
}

SparseMatrixR
Dta::get_complete_dar_matrix (py::array_t<int> start_intervals,
                              py::array_t<int> end_intervals, int num_intervals,
                              py::array_t<double> f)
{
  int _num_e_path = m_path_map.size ();
  int _num_e_link = m_link_vec.size ();
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  auto f_buf = f.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Dta::get_link_inflow, input dismension mismatch");
    }
  if (start_buf.shape[0] != end_buf.shape[0])
    {
      throw std::runtime_error (
        "Error, Dta::get_link_inflow, input length mismatch");
    }
  if (f_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Dta::get_link_inflow, input path flow mismatch");
    }
  int l = start_buf.shape[0];
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  double *f_ptr = (double *) f_buf.ptr;

  std::vector<Eigen::Triplet<double>> _record;
  _record.reserve (100000);
  for (int t = 0; t < l; ++t)
    {
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          if (end_ptr[t] < start_ptr[t])
            {
              throw std::runtime_error ("Error, Dta::get_dar_matrix, end time "
                                        "smaller than start time");
            }
          if (end_ptr[t] > get_cur_loading_interval ())
            {
              throw std::runtime_error (
                "Error, Dta::get_dar_matrix, loaded data not enough");
            }
          MNM_DTA_GRADIENT::add_dar_records_eigen (_record, m_link_vec[i],
                                                   m_path_map,
                                                   TFlt (start_ptr[t]),
                                                   TFlt (end_ptr[t]), i, t,
                                                   _num_e_link, _num_e_path,
                                                   f_ptr);
        }
    }
  SparseMatrixR mat (num_intervals * _num_e_link, num_intervals * _num_e_path);
  mat.setFromTriplets (_record.begin (), _record.end ());
  return mat;
}

// Multiclass DTA

class Mcdta
{
public:
  Mcdta ();
  ~Mcdta ();
  int initialize (std::string folder);
  int install_cc ();
  int install_cc_tree ();
  int run_whole ();
  int register_links (py::array_t<int> links);
  // FIXME: This returns a Numpy array for consistency, but it should really be
  // better to use a plain list.
  py::array_t<int> get_registered_links ();
  int get_cur_loading_interval ();
  py::array_t<double> get_emission_stats ();

  py::array_t<double> get_car_link_tt (py::array_t<double> start_intervals);
  py::array_t<double>
  get_car_link_tt_robust (py::array_t<double> start_intervals,
                          py::array_t<double> end_intervals);
  py::array_t<double> get_truck_link_tt (py::array_t<double> start_intervals);

  py::array_t<double> get_car_link_speed (py::array_t<double> start_intervals);
  py::array_t<double>
  get_truck_link_speed (py::array_t<double> start_intervals);

  py::array_t<double> get_link_car_inflow (py::array_t<int> start_intervals,
                                           py::array_t<int> end_intervals);
  py::array_t<double> get_link_truck_inflow (py::array_t<int> start_intervals,
                                             py::array_t<int> end_intervals);
  py::array_t<double> get_link_car_outflow (py::array_t<int> start_intervals,
                                            py::array_t<int> end_intervals);
  py::array_t<double> get_link_truck_outflow (py::array_t<int> start_intervals,
                                              py::array_t<int> end_intervals);

  py::array_t<double> get_car_link_in_cc (int link_ID);
  py::array_t<double> get_truck_link_in_cc (int link_ID);
  py::array_t<double> get_car_link_out_cc (int link_ID);
  py::array_t<double> get_truck_link_out_cc (int link_ID);

  int register_paths (py::array_t<int> paths);

  py::array_t<double> get_enroute_and_queue_veh_stats_agg ();
  py::array_t<double> get_queue_veh_each_link (py::array_t<int> useful_links,
                                               py::array_t<int> intervals);

  double get_car_link_out_num (int link_ID, double time);
  double get_truck_link_out_num (int link_ID, double time);

  py::array_t<double> get_car_dar_matrix (py::array_t<int> start_intervals,
                                          py::array_t<int> end_intervals);
  py::array_t<double> get_truck_dar_matrix (py::array_t<int> start_intervals,
                                            py::array_t<int> end_intervals);

  py::array_t<double> get_waiting_time_at_intersections ();
  py::array_t<int> get_link_spillback ();
  py::array_t<double> get_path_tt_car (py::array_t<int> link_IDs,
                                       py::array_t<double> start_intervals);
  py::array_t<double> get_path_tt_truck (py::array_t<int> link_IDs,
                                         py::array_t<double> start_intervals);

  MNM_Dta_Multiclass *m_mcdta;
  std::vector<MNM_Dlink_Multiclass *> m_link_vec;
  std::vector<MNM_Path *> m_path_vec;
  std::set<MNM_Path *> m_path_set;
  std::unordered_map<TInt, MNM_Path *> m_ID_path_mapping;
};

Mcdta::Mcdta ()
{
  m_mcdta = NULL;
  m_link_vec = std::vector<MNM_Dlink_Multiclass *> ();
  m_path_vec = std::vector<MNM_Path *> ();
  m_path_set = std::set<MNM_Path *> ();
  m_ID_path_mapping = std::unordered_map<TInt, MNM_Path *> ();
}

Mcdta::~Mcdta ()
{
  if (m_mcdta != NULL)
    {
      delete m_mcdta;
    }
  m_link_vec.clear ();
  m_path_vec.clear ();
}

int
Mcdta::initialize (std::string folder)
{
  m_mcdta = new MNM_Dta_Multiclass (folder);
  m_mcdta->build_from_files ();
  m_mcdta->hook_up_node_and_link ();
  m_mcdta->is_ok ();
  if (MNM_Routing_Fixed *_routing
      = dynamic_cast<MNM_Routing_Fixed *> (m_mcdta->m_routing))
    {
      MNM::get_ID_path_mapping (m_ID_path_mapping, _routing->m_path_table);
      return 0;
    }
  if (MNM_Routing_Hybrid *_routing
      = dynamic_cast<MNM_Routing_Hybrid *> (m_mcdta->m_routing))
    {
      // printf("start load ID path mapping\n");
      MNM::get_ID_path_mapping (m_ID_path_mapping,
                                _routing->m_routing_fixed->m_path_table);
      return 0;
      // printf("mapping size %d\n", m_ID_path_mapping.size());
    }
  if (MNM_Routing_Biclass_Hybrid *_routing
      = dynamic_cast<MNM_Routing_Biclass_Hybrid *> (m_mcdta->m_routing))
    {
      printf ("MNM_Routing_Biclass_Hybrid start load ID path mapping\n");
      MNM::get_ID_path_mapping (m_ID_path_mapping,
                                _routing->m_routing_fixed_car->m_path_table);
      std::cout << "MNM_Routing_Biclass_Hybrid mapping size "
                << m_ID_path_mapping.size () << std::endl;
      return 0;
    }
  // TODO: Check this.
  std::runtime_error ("Mcdta:: Routing type not implemented in API");
  return -1;
}

int
Mcdta::install_cc ()
{
  for (size_t i = 0; i < m_link_vec.size (); ++i)
    {
      m_link_vec[i]->install_cumulative_curve_multiclass ();
    }
  return 0;
}

int
Mcdta::install_cc_tree ()
{
  for (size_t i = 0; i < m_link_vec.size (); ++i)
    {
      m_link_vec[i]->install_cumulative_curve_tree_multiclass ();
    }
  return 0;
}

int
Mcdta::run_whole ()
{
  m_mcdta->pre_loading ();
  m_mcdta->loading (false);
  return 0;
}

int
Mcdta::register_links (py::array_t<int> links)
{
  if (m_link_vec.size () > 0)
    {
      printf ("Warning, Dta::register_links, link exists\n");
      m_link_vec.clear ();
    }
  auto links_buf = links.request ();
  if (links_buf.ndim != 1)
    {
      throw std::runtime_error ("Number of dimensions must be one");
    }
  int *links_ptr = (int *) links_buf.ptr;
  MNM_Dlink *_link;
  for (int i = 0; i < links_buf.shape[0]; i++)
    {
      _link = m_mcdta->m_link_factory->get_link (TInt (links_ptr[i]));
      if (MNM_Dlink_Multiclass *_mclink
          = dynamic_cast<MNM_Dlink_Multiclass *> (_link))
        {
          if (std::find (m_link_vec.begin (), m_link_vec.end (), _link)
              != m_link_vec.end ())
            {
              throw std::runtime_error (
                "Error, Dta::register_links, link exists");
            }
          else
            {
              m_link_vec.push_back (_mclink);
            }
        }
      else
        {
          throw std::runtime_error (
            "Mcdta::register_links: link type is not multiclass");
        }
    }
  return 0;
}

py::array_t<int>
Mcdta::get_registered_links ()
{
  auto results = py::array_t<int> (m_link_vec.size ());
  auto results_buf = results.request ();
  int *results_ptr = static_cast<int *> (results_buf.ptr);
  for (int idx = 0; idx < m_link_vec.size (); idx++)
    results_ptr[idx] = (int) m_link_vec[idx]->m_link_ID;
  return results;
}

int
Mcdta::get_cur_loading_interval ()
{
  return m_mcdta->m_current_loading_interval ();
}

py::array_t<double>
Mcdta::get_emission_stats ()
{
  TInt _count_car = 0, _count_truck = 0;
  TFlt _tot_tt_car = 0.0, _tot_tt_truck = 0.0;
  MNM_Veh_Multiclass *_veh;
  int _end_time = get_cur_loading_interval ();
  for (auto _map_it : m_mcdta->m_veh_factory->m_veh_map)
    {
      _veh = dynamic_cast<MNM_Veh_Multiclass *> (_map_it.second);
      if (_veh->m_class == 0)
        {
          _count_car += 1;
          if (_veh->m_finish_time > 0)
            {
              _tot_tt_car
                += (_veh->m_finish_time - _veh->m_start_time) * 5.0 / 3600.0;
            }
          else
            {
              _tot_tt_car += (_end_time - _veh->m_start_time) * 5.0 / 3600.0;
            }
        }
      else
        {
          _count_truck += 1;
          if (_veh->m_finish_time > 0)
            {
              _tot_tt_truck
                += (_veh->m_finish_time - _veh->m_start_time) * 5.0 / 3600.0;
            }
          else
            {
              _tot_tt_truck += (_end_time - _veh->m_start_time) * 5.0 / 3600.0;
            }
        }
    }
  //     printf("\n\nTotal car: %d, Total truck: %d, Total car tt: %.2f hours,
  //     Total truck tt: %.2f hours\n\n",
  //            int(_count_car/m_mcdta -> m_flow_scalar),
  //            int(_count_truck/m_mcdta -> m_flow_scalar),
  //            float(_tot_tt_car/m_mcdta -> m_flow_scalar),
  //            float(_tot_tt_truck/m_mcdta -> m_flow_scalar));
  //     m_mcdta -> m_emission -> output();

  int new_shape[1] = { 4 };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  result_ptr[0] = _count_car / m_mcdta->m_flow_scalar;
  result_ptr[1] = _count_truck / m_mcdta->m_flow_scalar;
  result_ptr[2] = _tot_tt_car / m_mcdta->m_flow_scalar;
  result_ptr[3] = _tot_tt_truck / m_mcdta->m_flow_scalar;

  return result;
}

py::array_t<double>
Mcdta::get_waiting_time_at_intersections ()
{
  int new_shape[1] = { (int) m_link_vec.size () };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  for (size_t i = 0; i < m_link_vec.size (); ++i)
    {
      result_ptr[i]
        = MNM_DTA_GRADIENT::get_average_waiting_time_at_intersection (
          m_link_vec[i]) ();
    }

  return result;
}

py::array_t<int>
Mcdta::get_link_spillback ()
{
  int new_shape[1] = { (int) m_link_vec.size () };
  auto result = py::array_t<int> (new_shape);
  auto result_buf = result.request ();
  int *result_ptr = (int *) result_buf.ptr;
  for (size_t i = 0; i < m_link_vec.size (); ++i)
    {
      result_ptr[i] = MNM_DTA_GRADIENT::get_is_spillback (m_link_vec[i]) ();
    }

  return result;
}

py::array_t<double>
Mcdta::get_path_tt_car (py::array_t<int> link_IDs,
                        py::array_t<double> start_intervals)
{
  auto start_buf = start_intervals.request ();
  int num_int = start_buf.shape[0];

  auto links_buf = link_IDs.request ();
  int num_link = links_buf.shape[0];

  int new_shape[1] = { num_link };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;

  double *start_ptr = (double *) start_buf.ptr;
  int *links_ptr = (int *) links_buf.ptr;
  MNM_Dlink *_link;
  for (int i = 0; i < links_buf.shape[0]; i++)
    {
      _link = m_mcdta->m_link_factory->get_link (TInt (links_ptr[i]));
      if (MNM_Dlink_Multiclass *_mclink
          = dynamic_cast<MNM_Dlink_Multiclass *> (_link))
        {
          double avg_tt = 0;
          for (int t = 0; t < num_int; ++t)
            {
              double _tmp
                = MNM_DTA_GRADIENT::get_travel_time_car (_mclink,
                                                         TFlt (start_ptr[t])) ()
                  * 5;
              if (_tmp > 20 * (_mclink->m_length / _mclink->m_ffs_car))
                {
                  _tmp = 20 * _mclink->m_length / _mclink->m_ffs_car;
                }
              avg_tt += _tmp; // seconds
            }
          avg_tt /= num_int;
          result_ptr[i] = avg_tt;
        }
      else
        {
          throw std::runtime_error (
            "Mcdta::register_links: link type is not multiclass");
        }
    }

  return result;
}

py::array_t<double>
Mcdta::get_path_tt_truck (py::array_t<int> link_IDs,
                          py::array_t<double> start_intervals)
{
  auto start_buf = start_intervals.request ();
  int num_int = start_buf.shape[0];

  auto links_buf = link_IDs.request ();
  int num_link = links_buf.shape[0];

  int new_shape[1] = { num_link };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;

  double *start_ptr = (double *) start_buf.ptr;
  int *links_ptr = (int *) links_buf.ptr;
  MNM_Dlink *_link;
  for (int i = 0; i < links_buf.shape[0]; i++)
    {
      _link = m_mcdta->m_link_factory->get_link (TInt (links_ptr[i]));
      if (MNM_Dlink_Multiclass *_mclink
          = dynamic_cast<MNM_Dlink_Multiclass *> (_link))
        {
          double avg_tt = 0;
          for (int t = 0; t < num_int; ++t)
            {
              double _tmp
                = MNM_DTA_GRADIENT::get_travel_time_truck (_mclink,
                                                           TFlt (
                                                             start_ptr[t])) ()
                  * 5;
              if (_tmp > 20 * (_mclink->m_length / _mclink->m_ffs_truck))
                {
                  _tmp = 20 * _mclink->m_length / _mclink->m_ffs_truck;
                }
              avg_tt += _tmp; // seconds
            }
          avg_tt /= num_int;
          result_ptr[i] = avg_tt;
        }
      else
        {
          throw std::runtime_error (
            "Mcdta::register_links: link type is not multiclass");
        }
    }

  return result;
}

// unit: m_mcdta -> m_unit_time (eg: 5 seconds)
py::array_t<double>
Mcdta::get_car_link_tt (py::array_t<double> start_intervals)
{
  auto start_buf = start_intervals.request ();
  if (start_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_car_link_tt, input dismension mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };

  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  double *start_ptr = (double *) start_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (start_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error (
            "Error, Mcdta::get_car_link_tt, loaded data not enough");
        }
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          double _tmp
            = MNM_DTA_GRADIENT::get_travel_time_car (m_link_vec[i],
                                                     TFlt (start_ptr[t])) ();
          // if (_tmp * 5 > 20 * (m_link_vec[i] -> m_length / m_link_vec[i] ->
          // m_ffs_car)){
          //     _tmp = 20 * m_link_vec[i] -> m_length / m_link_vec[i] ->
          //     m_ffs_car / 5.0;
          // }
          result_ptr[i * l + t] = _tmp * 5;
        }
    }
  return result;
}

py::array_t<double>
Mcdta::get_car_link_tt_robust (py::array_t<double> start_intervals,
                               py::array_t<double> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    {
      throw std::runtime_error ("Error, Mcdta::get_car_link_tt_robust, "
                                "input dismension mismatch");
    }
  if (start_buf.shape[0] != end_buf.shape[0])
    {
      throw std::runtime_error (
        "Error, Mcdta::get_car_link_tt_robust, input length mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };

  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  double *start_ptr = (double *) start_buf.ptr;
  double *end_ptr = (double *) end_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (start_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error (
            "Error, Mcdta::get_car_link_tt_robust, loaded data not "
            "enough");
        }
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          double _tmp
            = MNM_DTA_GRADIENT::get_travel_time_car_robust (m_link_vec[i],
                                                            TFlt (start_ptr[t]),
                                                            TFlt (
                                                              end_ptr[t])) ();
          // if (_tmp * 5 > 20 * (m_link_vec[i] -> m_length / m_link_vec[i] ->
          // m_ffs_car)){
          //     _tmp = 20 * m_link_vec[i] -> m_length / m_link_vec[i] ->
          //     m_ffs_car / 5.0;
          // }
          result_ptr[i * l + t] = _tmp * 5;
        }
    }
  return result;
}

// unit: m_mcdta -> m_unit_time (eg: 5 seconds)
py::array_t<double>
Mcdta::get_truck_link_tt (py::array_t<double> start_intervals)
{
  auto start_buf = start_intervals.request ();
  if (start_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_truck_link_tt, input dismension mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };

  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  double *start_ptr = (double *) start_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (start_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error (
            "Error, Mcdta::get_truck_link_tt, loaded data not enough");
        }
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          double _tmp
            = MNM_DTA_GRADIENT::get_travel_time_truck (m_link_vec[i],
                                                       TFlt (start_ptr[t])) ();
          // if (_tmp * 5 > 20 * (m_link_vec[i] -> m_length / m_link_vec[i] ->
          // m_ffs_truck)){
          //     _tmp = 20 * m_link_vec[i] -> m_length / m_link_vec[i] ->
          //     m_ffs_truck / 5.0;
          // }
          result_ptr[i * l + t] = _tmp * 5;
        }
    }
  return result;
}

py::array_t<double>
Mcdta::get_car_link_speed (py::array_t<double> start_intervals)
{
  auto start_buf = start_intervals.request ();
  if (start_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_car_link_speed, input dismension mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };

  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  double *start_ptr = (double *) start_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (start_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error (
            "Error, Mcdta::get_car_link_speed, loaded data not enough");
        }
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          int _tt
            = MNM_DTA_GRADIENT::get_travel_time_car (m_link_vec[i],
                                                     TFlt (start_ptr[t])) ()
              * m_mcdta->m_unit_time; // seconds
          result_ptr[i * l + t]
            = (m_link_vec[i]->m_length) / _tt * 3600 / 1600; // mile per hour
        }
    }
  return result;
}

py::array_t<double>
Mcdta::get_truck_link_speed (py::array_t<double> start_intervals)
{
  auto start_buf = start_intervals.request ();
  if (start_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_truck_link_speed, input dismension mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };

  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  double *start_ptr = (double *) start_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (start_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error ("Error, Mcdta::get_truck_link_speed, "
                                    "loaded data not enough");
        }
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          int _tt
            = MNM_DTA_GRADIENT::get_travel_time_truck (m_link_vec[i],
                                                       TFlt (start_ptr[t])) ()
              * m_mcdta->m_unit_time; // seconds
          result_ptr[i * l + t]
            = (m_link_vec[i]->m_length) / _tt * 3600 / 1600; // mile per hour
        }
    }
  return result;
}

py::array_t<double>
Mcdta::get_link_car_inflow (py::array_t<int> start_intervals,
                            py::array_t<int> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_link_car_inflow, input dismension mismatch");
    }
  if (start_buf.shape[0] != end_buf.shape[0])
    {
      throw std::runtime_error (
        "Error, Mcdta::get_link_car_inflow, input length mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (end_ptr[t] < start_ptr[t])
        {
          throw std::runtime_error ("Error, Mcdta::get_link_car_inflow, "
                                    "end time smaller than start time");
        }
      if (end_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error (
            "Error, Mcdta::get_link_car_inflow, loaded data not enough");
        }
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          result_ptr[i * l + t]
            = MNM_DTA_GRADIENT::get_link_inflow_car (m_link_vec[i],
                                                     TFlt (start_ptr[t]),
                                                     TFlt (end_ptr[t])) ();
          // printf("i %d, t %d, %f\n", i, t, result_ptr[i * l + t]);
        }
    }
  return result;
}

py::array_t<double>
Mcdta::get_link_truck_inflow (py::array_t<int> start_intervals,
                              py::array_t<int> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    {
      throw std::runtime_error ("Error, Mcdta::get_link_truck_inflow, "
                                "input dismension mismatch");
    }
  if (start_buf.shape[0] != end_buf.shape[0])
    {
      throw std::runtime_error (
        "Error, Mcdta::get_link_truck_inflow, input length mismatch");
    }
  int l = start_buf.shape[0];
  int new_shape[2] = { (int) m_link_vec.size (), l };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (end_ptr[t] < start_ptr[t])
        {
          throw std::runtime_error ("Error, Mcdta::get_link_truck_inflow, "
                                    "end time smaller than start time");
        }
      if (end_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error ("Error, Mcdta::get_link_truck_inflow, "
                                    "loaded data not enough");
        }
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          result_ptr[i * l + t]
            = MNM_DTA_GRADIENT::get_link_inflow_truck (m_link_vec[i],
                                                       TFlt (start_ptr[t]),
                                                       TFlt (end_ptr[t])) ();
          // printf("i %d, t %d, %f\n", i, t, result_ptr[i * l + t]);
        }
    }
  return result;
}

py::array_t<double>
Mcdta::get_link_car_outflow (py::array_t<int> start_intervals,
                             py::array_t<int> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    throw std::runtime_error ("input dismensions mismatch");
  if (start_buf.shape[0] != end_buf.shape[0])
    throw std::runtime_error ("input lengths mismatch");
  int l = start_buf.shape[0];
  int new_shape[] = { (int) m_link_vec.size (), l };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (end_ptr[t] < start_ptr[t])
        throw std::runtime_error ("end time smaller than start time");
      if (end_ptr[t] > get_cur_loading_interval ())
        throw std::runtime_error ("loaded data not enough");
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        result_ptr[i * l + t]
          = MNM_DTA_GRADIENT::get_link_outflow_car (m_link_vec[i],
                                                    TFlt (start_ptr[t]),
                                                    TFlt (end_ptr[t])) ();
    }
  return result;
}

py::array_t<double>
Mcdta::get_link_truck_outflow (py::array_t<int> start_intervals,
                               py::array_t<int> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    throw std::runtime_error ("input dismensions mismatch");
  if (start_buf.shape[0] != end_buf.shape[0])
    throw std::runtime_error ("input lengths mismatch");
  int l = start_buf.shape[0];
  int new_shape[] = { (int) m_link_vec.size (), l };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  for (int t = 0; t < l; ++t)
    {
      if (end_ptr[t] < start_ptr[t])
        throw std::runtime_error ("end time smaller than start time");
      if (end_ptr[t] > get_cur_loading_interval ())
        throw std::runtime_error ("loaded data not enough");
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        result_ptr[i * l + t]
          = MNM_DTA_GRADIENT::get_link_outflow_truck (m_link_vec[i],
                                                      TFlt (start_ptr[t]),
                                                      TFlt (end_ptr[t])) ();
    }
  return result;
}

py::array_t<double>
Mcdta::get_car_link_in_cc (int link_ID)
{
  auto l = dynamic_cast<MNM_Dlink_Multiclass *> (
    m_mcdta->m_link_factory->get_link (TInt (link_ID)));
  if (!l->m_N_in_car)
    throw std::runtime_error ("cc not installed");
  auto rec = l->m_N_in_car->m_recorder;
  int shape[2] = { (int) rec.size (), 2 };
  auto result = py::array_t<double> (shape);
  double *result_ptr = (double *) result.request ().ptr;
  for (size_t i = 0; i < rec.size (); ++i)
    {
      result_ptr[i * 2] = rec[i].first ();
      result_ptr[i * 2 + 1] = rec[i].second ();
    }
  return result;
}

py::array_t<double>
Mcdta::get_truck_link_in_cc (int link_ID)
{
  auto l = dynamic_cast<MNM_Dlink_Multiclass *> (
    m_mcdta->m_link_factory->get_link (TInt (link_ID)));
  if (!l->m_N_in_truck)
    throw std::runtime_error ("cc not installed");
  auto rec = l->m_N_in_truck->m_recorder;
  int shape[2] = { (int) rec.size (), 2 };
  auto result = py::array_t<double> (shape);
  double *result_ptr = (double *) result.request ().ptr;
  for (size_t i = 0; i < rec.size (); ++i)
    {
      result_ptr[i * 2] = rec[i].first ();
      result_ptr[i * 2 + 1] = rec[i].second ();
    }
  return result;
}

py::array_t<double>
Mcdta::get_car_link_out_cc (int link_ID)
{
  auto l = dynamic_cast<MNM_Dlink_Multiclass *> (
    m_mcdta->m_link_factory->get_link (TInt (link_ID)));
  if (!l->m_N_out_car)
    throw std::runtime_error ("cc not installed");
  auto rec = l->m_N_out_car->m_recorder;
  int shape[2] = { (int) rec.size (), 2 };
  auto result = py::array_t<double> (shape);
  double *result_ptr = (double *) result.request ().ptr;
  for (size_t i = 0; i < rec.size (); ++i)
    {
      result_ptr[i * 2] = rec[i].first ();
      result_ptr[i * 2 + 1] = rec[i].second ();
    }
  return result;
}

py::array_t<double>
Mcdta::get_truck_link_out_cc (int link_ID)
{
  auto l = dynamic_cast<MNM_Dlink_Multiclass *> (
    m_mcdta->m_link_factory->get_link (TInt (link_ID)));
  if (!l->m_N_out_truck)
    throw std::runtime_error ("cc not installed");
  auto rec = l->m_N_out_truck->m_recorder;
  int shape[2] = { (int) rec.size (), 2 };
  auto result = py::array_t<double> (shape);
  double *result_ptr = (double *) result.request ().ptr;
  for (size_t i = 0; i < rec.size (); ++i)
    {
      result_ptr[i * 2] = rec[i].first ();
      result_ptr[i * 2 + 1] = rec[i].second ();
    }
  return result;
}
int
Mcdta::register_paths (py::array_t<int> paths)
{
  if (m_path_vec.size () > 0)
    {
      printf ("Warning, Mcdta::register_paths, path exists\n");
      m_path_vec.clear ();
      m_path_set.clear ();
    }
  auto paths_buf = paths.request ();
  if (paths_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Mcdta::register_paths: Number of dimensions must be one");
    }
  int *paths_ptr = (int *) paths_buf.ptr;
  TInt _path_ID;
  for (int i = 0; i < paths_buf.shape[0]; i++)
    {
      _path_ID = TInt (paths_ptr[i]);
      // printf("registering path %d, %d\n", _path_ID(),
      // (int)m_ID_path_mapping.size());
      if (m_ID_path_mapping.find (_path_ID) == m_ID_path_mapping.end ())
        {
          throw std::runtime_error ("Mcdta::register_paths: No such path");
        }
      else
        {
          m_path_vec.push_back (m_ID_path_mapping[_path_ID]);
        }
    }
  m_path_set = std::set<MNM_Path *> (m_path_vec.begin (), m_path_vec.end ());
  return 0;
}

py::array_t<double>
Mcdta::get_enroute_and_queue_veh_stats_agg ()
{
  int _tot_interval = get_cur_loading_interval ();
  int new_shape[2] = { _tot_interval, 3 };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;

  if ((int) m_mcdta->m_enroute_veh_num.size () != get_cur_loading_interval ())
    {
      throw std::runtime_error (
        "Error, Mcdta::get_enroute_and_queue_veh_stats_agg, enroute "
        "vehicle missed for some intervals");
    }
  else if ((int) m_mcdta->m_queue_veh_num.size ()
           != get_cur_loading_interval ())
    {
      throw std::runtime_error (
        "Error, Mcdta::get_enroute_and_queue_veh_stats_agg, queuing "
        "vehicle missed for some intervals");
    }
  else
    {
      for (int i = 0; i < _tot_interval; ++i)
        {
          result_ptr[i * 3]
            = (m_mcdta->m_enroute_veh_num[i]()) / (m_mcdta->m_flow_scalar);
          result_ptr[i * 3 + 1]
            = (m_mcdta->m_queue_veh_num[i]()) / (m_mcdta->m_flow_scalar);
          result_ptr[i * 3 + 2]
            = (m_mcdta->m_enroute_veh_num[i]() - m_mcdta->m_queue_veh_num[i]())
              / (m_mcdta->m_flow_scalar);
        }
    }
  return result;
}

py::array_t<double>
Mcdta::get_queue_veh_each_link (py::array_t<int> useful_links,
                                py::array_t<int> intervals)
{
  auto intervals_buf = intervals.request ();
  if (intervals_buf.ndim != 1)
    {
      throw std::runtime_error ("Error, Mcdta::get_queue_veh_each_link, "
                                "input (intervals) dismension mismatch");
    }
  auto links_buf = useful_links.request ();
  if (links_buf.ndim != 1)
    {
      throw std::runtime_error ("Error, Mcdta::get_queue_veh_each_link, "
                                "input (useful_links) dismension mismatch");
    }
  int num_intervals = intervals_buf.shape[0];
  int num_links = links_buf.shape[0];
  int new_shape[2] = { num_links, num_intervals };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  double *intervals_ptr = (double *) intervals_buf.ptr;
  double *links_ptr = (double *) links_buf.ptr;

  for (int t = 0; t < num_intervals; ++t)
    {
      if (intervals_ptr[t] > get_cur_loading_interval ())
        {
          throw std::runtime_error (
            "Error, Mcdta::get_queue_veh_each_link, too large interval "
            "number");
        }
      for (int i = 0; i < num_links; ++i)
        {
          if (m_mcdta->m_queue_veh_map.find (links_ptr[i])
              == m_mcdta->m_queue_veh_map.end ())
            {
              throw std::runtime_error (
                "Error, Mcdta::get_queue_veh_each_link, can't find link "
                "ID");
            }
          result_ptr[i * num_intervals + t]
            = (*(m_mcdta->m_queue_veh_map[links_ptr[i]]))[intervals_ptr[t]];
        }
    }
  return result;
}

double
Mcdta::get_car_link_out_num (int link_ID, double time)
{
  MNM_Dlink_Multiclass *_link
    = (MNM_Dlink_Multiclass *) m_mcdta->m_link_factory->get_link (
      TInt (link_ID));
  // printf("%d\n", _link -> m_link_ID());
  if (_link->m_N_out_car == NULL)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_car_link_out_cc, cc not installed");
    }
  // printf("1\n");
  TFlt result = _link->m_N_out_car->get_result (TFlt (time));
  // printf("%lf\n", result());
  return result ();
}

double
Mcdta::get_truck_link_out_num (int link_ID, double time)
{
  MNM_Dlink_Multiclass *_link
    = (MNM_Dlink_Multiclass *) m_mcdta->m_link_factory->get_link (
      TInt (link_ID));
  if (_link->m_N_out_truck == NULL)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_truck_link_out_cc, cc not installed");
    }
  TFlt result = _link->m_N_out_truck->get_result (TFlt (time));
  return result ();
}

py::array_t<double>
Mcdta::get_car_dar_matrix (py::array_t<int> start_intervals,
                           py::array_t<int> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_car_dar_matrix, input dismension mismatch");
    }
  if (start_buf.shape[0] != end_buf.shape[0])
    {
      throw std::runtime_error (
        "Error, Mcdta::get_car_dar_matrix, input length mismatch");
    }
  int l = start_buf.shape[0];
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  std::vector<dar_record *> _record = std::vector<dar_record *> ();
  // for (size_t i = 0; i<m_link_vec.size(); ++i){
  //   m_link_vec[i] -> m_N_in_tree -> print_out();
  // }
  for (int t = 0; t < l; ++t)
    {
      // printf("Current processing time: %d\n", t);
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          if (end_ptr[t] < start_ptr[t])
            {
              throw std::runtime_error (
                "Error, Mcdta::get_car_dar_matrix, end time smaller "
                "than start time");
            }
          if (end_ptr[t] > get_cur_loading_interval ())
            {
              throw std::runtime_error (
                "Error, Mcdta::get_car_dar_matrix, loaded data not "
                "enough");
            }
          MNM_DTA_GRADIENT::add_dar_records_car (_record, m_link_vec[i],
                                                 m_path_set,
                                                 TFlt (start_ptr[t]),
                                                 TFlt (end_ptr[t]));
        }
    }
  // path_ID, assign_time, link_ID, start_int, flow
  int new_shape[2] = { (int) _record.size (), 5 };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  dar_record *tmp_record;
  for (size_t i = 0; i < _record.size (); ++i)
    {
      tmp_record = _record[i];
      result_ptr[i * 5 + 0] = (double) tmp_record->path_ID ();
      result_ptr[i * 5 + 1] = (double) tmp_record->assign_int ();
      result_ptr[i * 5 + 2] = (double) tmp_record->link_ID ();
      result_ptr[i * 5 + 3] = (double) tmp_record->link_start_int ();
      result_ptr[i * 5 + 4] = tmp_record->flow ();
    }
  for (size_t i = 0; i < _record.size (); ++i)
    {
      delete _record[i];
    }
  _record.clear ();
  return result;
}

py::array_t<double>
Mcdta::get_truck_dar_matrix (py::array_t<int> start_intervals,
                             py::array_t<int> end_intervals)
{
  auto start_buf = start_intervals.request ();
  auto end_buf = end_intervals.request ();
  if (start_buf.ndim != 1 || end_buf.ndim != 1)
    {
      throw std::runtime_error (
        "Error, Mcdta::get_truck_dar_matrix, input dismension mismatch");
    }
  if (start_buf.shape[0] != end_buf.shape[0])
    {
      throw std::runtime_error (
        "Error, Mcdta::get_truck_dar_matrix, input length mismatch");
    }
  int l = start_buf.shape[0];
  int *start_ptr = (int *) start_buf.ptr;
  int *end_ptr = (int *) end_buf.ptr;
  std::vector<dar_record *> _record = std::vector<dar_record *> ();
  // for (size_t i = 0; i<m_link_vec.size(); ++i){
  //   m_link_vec[i] -> m_N_in_tree -> print_out();
  // }
  for (int t = 0; t < l; ++t)
    {
      for (size_t i = 0; i < m_link_vec.size (); ++i)
        {
          if (end_ptr[t] < start_ptr[t])
            {
              throw std::runtime_error (
                "Error, Mcdta::get_truck_dar_matrix, end time smaller "
                "than start time");
            }
          if (end_ptr[t] > get_cur_loading_interval ())
            {
              throw std::runtime_error (
                "Error, Mcdta::get_truck_dar_matrix, loaded data not "
                "enough");
            }
          MNM_DTA_GRADIENT::add_dar_records_truck (_record, m_link_vec[i],
                                                   m_path_set,
                                                   TFlt (start_ptr[t]),
                                                   TFlt (end_ptr[t]));
        }
    }
  // path_ID, assign_time, link_ID, start_int, flow
  int new_shape[2] = { (int) _record.size (), 5 };
  auto result = py::array_t<double> (new_shape);
  auto result_buf = result.request ();
  double *result_ptr = (double *) result_buf.ptr;
  dar_record *tmp_record;
  for (size_t i = 0; i < _record.size (); ++i)
    {
      tmp_record = _record[i];
      result_ptr[i * 5 + 0] = (double) tmp_record->path_ID ();
      result_ptr[i * 5 + 1] = (double) tmp_record->assign_int ();
      result_ptr[i * 5 + 2] = (double) tmp_record->link_ID ();
      result_ptr[i * 5 + 3] = (double) tmp_record->link_start_int ();
      result_ptr[i * 5 + 4] = tmp_record->flow ();
    }
  for (size_t i = 0; i < _record.size (); ++i)
    {
      delete _record[i];
    }
  _record.clear ();
  return result;
}

// Entry point

PYBIND11_MODULE (_macposts_ext, m)
{
  m.def ("set_random_state", &MNM_Ults::srand, R"pbdoc(Set the random state.

Note that this only affects the random number generator used during network
simulation.

)pbdoc");

  py::class_<Dta> (m, "Dta")
    .def (py::init<> ())
    .def ("initialize", &Dta::initialize)
    .def ("run_whole", &Dta::run_whole)
    .def ("install_cc", &Dta::install_cc)
    .def ("install_cc_tree", &Dta::install_cc_tree)
    .def ("get_cur_loading_interval", &Dta::get_cur_loading_interval)
    .def ("register_links", &Dta::register_links)
    .def_property_readonly ("registered_links", &Dta::get_registered_links,
                            "IDs of previously registered links.")
    .def ("register_paths", &Dta::register_paths)
    .def ("get_link_tt", &Dta::get_link_tt)
    .def ("get_path_tt", &Dta::get_path_tt)
    .def ("get_link_inflow", &Dta::get_link_inflow)
    .def ("get_link_in_cc", &Dta::get_link_in_cc)
    .def ("get_link_out_cc", &Dta::get_link_out_cc)
    .def ("get_dar_matrix", &Dta::get_dar_matrix)
    .def ("get_complete_dar_matrix", &Dta::get_complete_dar_matrix);

  py::class_<Mcdta> (m, "Mcdta")
    .def (py::init<> ())
    .def ("initialize", &Mcdta::initialize)
    .def ("run_whole", &Mcdta::run_whole)
    .def ("install_cc", &Mcdta::install_cc)
    .def ("install_cc_tree", &Mcdta::install_cc_tree)
    .def ("get_emission_stats", &Mcdta::get_emission_stats)
    .def ("get_cur_loading_interval", &Mcdta::get_cur_loading_interval)
    .def ("register_links", &Mcdta::register_links)
    .def_property_readonly ("registered_links", &Mcdta::get_registered_links,
                            "IDs of previously registered links.")
    .def ("register_paths", &Mcdta::register_paths)
    .def ("get_car_link_tt", &Mcdta::get_car_link_tt)
    .def ("get_car_link_tt_robust", &Mcdta::get_car_link_tt_robust)
    .def ("get_truck_link_tt", &Mcdta::get_truck_link_tt)
    .def ("get_car_link_out_num", &Mcdta::get_car_link_out_num)
    .def ("get_truck_link_out_num", &Mcdta::get_truck_link_out_num)
    .def ("get_car_link_speed", &Mcdta::get_car_link_speed)
    .def ("get_truck_link_speed", &Mcdta::get_truck_link_speed)
    .def ("get_link_car_inflow", &Mcdta::get_link_car_inflow)
    .def ("get_link_truck_inflow", &Mcdta::get_link_truck_inflow)
    .def ("get_link_car_outflow", &Mcdta::get_link_car_outflow)
    .def ("get_link_truck_outflow", &Mcdta::get_link_truck_outflow)
    .def ("get_enroute_and_queue_veh_stats_agg",
          &Mcdta::get_enroute_and_queue_veh_stats_agg)
    .def ("get_queue_veh_each_link", &Mcdta::get_queue_veh_each_link)
    .def ("get_car_link_out_num", &Mcdta::get_car_link_out_num)
    .def ("get_truck_link_out_num", &Mcdta::get_truck_link_out_num)
    .def ("get_car_link_in_cc", &Mcdta::get_car_link_in_cc)
    .def ("get_truck_link_in_cc", &Mcdta::get_truck_link_in_cc)
    .def ("get_car_link_out_cc", &Mcdta::get_car_link_out_cc)
    .def ("get_truck_link_out_cc", &Mcdta::get_truck_link_out_cc)
    .def ("get_car_dar_matrix", &Mcdta::get_car_dar_matrix)
    .def ("get_truck_dar_matrix", &Mcdta::get_truck_dar_matrix)

    // For scenarios in McKees Rocks project:
    .def ("get_waiting_time_at_intersections",
          &Mcdta::get_waiting_time_at_intersections)
    .def ("get_link_spillback", &Mcdta::get_link_spillback)
    .def ("get_path_tt_car", &Mcdta::get_path_tt_car)
    .def ("get_path_tt_truck", &Mcdta::get_path_tt_truck);
}
