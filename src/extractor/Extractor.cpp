#include "Extractor.h"
#include "common/Utils.hpp"
#include "common/VortexTransition.h"
#include "io/GLDataset.h"
#include <set>
#include <climits>
#include <cstdlib>
#include <cassert>
#include <cstring>

VortexExtractor::VortexExtractor() :
  _dataset(NULL), 
  _gauge(false), 
  _archive(true)
{

}

VortexExtractor::~VortexExtractor()
{

}

void VortexExtractor::SetDataset(const GLDatasetBase* ds)
{
  _dataset = ds;
}

void VortexExtractor::SetGaugeTransformation(bool g)
{
  _gauge = g; 
}

void VortexExtractor::SetArchive(bool a)
{
  _archive = a;
}

void VortexExtractor::SaveVortexLines(int slot)
{
  const GLDatasetBase *ds = _dataset;
  std::ostringstream os; 
  os << ds->DataName() << ".vlines." << ds->TimeStep(slot);
  
  std::vector<VortexObject> &vobjs = 
    slot == 0 ? _vortex_objects : _vortex_objects1;
  std::vector<VortexLine> &vlines = 
    slot == 0 ? _vortex_lines : _vortex_lines1;
  std::map<FaceIdType, PuncturedFace> &pfs =
    slot == 0 ? _punctured_faces : _punctured_faces1;

  VortexObjectsToVortexLines(pfs, vobjs, vlines);

  std::string info;
  Dataset()->SerializeDataInfoToString(info);

  ::SaveVortexLines(vlines, info, os.str());
}

void VortexExtractor::ClearPuncturedObjects()
{
  _punctured_edges.clear();
  _punctured_faces.clear();
  _punctured_faces1.clear();
}

bool VortexExtractor::SavePuncturedEdges() const
{
  const GLDatasetBase *ds = _dataset;
  std::ostringstream os; 
  os << ds->DataName() << ".pe." << ds->TimeStep(0) << "." << ds->TimeStep(1);
  return ::SavePuncturedEdges(_punctured_edges, os.str());
}

bool VortexExtractor::SavePuncturedFaces(int slot) const
{
  const GLDatasetBase *ds = _dataset;
  std::ostringstream os; 
  os << ds->DataName() << ".pf." << ds->TimeStep(slot);
  bool succ = ::SavePuncturedFaces(
      slot == 0 ? _punctured_faces : _punctured_faces1, os.str());

  if (!succ) 
    fprintf(stderr, "failed to read punctured faces from file %s\n", os.str().c_str());
  return succ;
}

bool VortexExtractor::LoadPuncturedEdges()
{
  const GLDatasetBase *ds = _dataset;
  std::ostringstream os; 
  os << ds->DataName() << ".pe." << ds->TimeStep(0) << "." << ds->TimeStep(1);
 
  std::map<EdgeIdType, PuncturedEdge> m;
  if (!::LoadPuncturedEdges(m, os.str())) return false;
  
  for (std::map<EdgeIdType, PuncturedEdge>::iterator it = m.begin(); it != m.end(); it ++) 
    AddPuncturedEdge(it->first, it->second.chirality, it->second.t);
  
  return true;
}

bool VortexExtractor::LoadPuncturedFaces(int slot)
{
  const GLDatasetBase *ds = _dataset;
  std::ostringstream os; 
  os << ds->DataName() << ".pf." << ds->TimeStep(slot);
  
  std::map<FaceIdType, PuncturedFace> m; 

  if (!::LoadPuncturedFaces(m, os.str())) return false;

  for (std::map<FaceIdType, PuncturedFace>::iterator it = m.begin(); it != m.end(); it ++) {
    AddPuncturedFace(it->first, slot, it->second.chirality, it->second.pos);
  }

  return true;
}

void VortexExtractor::AddPuncturedFace(FaceIdType id, int slot, ChiralityType chirality, const double pos[])
{
  // face
  PuncturedFace pf;

  pf.chirality = chirality;
  memcpy(pf.pos, pos, sizeof(double)*3);

  if (slot == 0) _punctured_faces[id] = pf;
  else _punctured_faces1[id] = pf;

  // vcell
  PuncturedCell &vc = _punctured_vcells[id];
  if (slot == 0) vc.SetChirality(0, -chirality);
  else vc.SetChirality(1, chirality);

  // cell
  const MeshGraph *mg = _dataset->MeshGraph();
  const CFace &face = mg->Face(id);
  for (int i=0; i<face.contained_cells.size(); i++) {
    CellIdType cid = face.contained_cells[i]; 
    if (cid == UINT_MAX) continue;

    int fchirality = face.contained_cells_chirality[i];
    int fid = face.contained_cells_fid[i];
    
    // bool found = _punctured_cells.find(cid) != _punctured_cells.end();
    // fprintf(stderr, "cid=%u, found=%d\n", cid, found);
    
    PuncturedCell &c = slot == 0 ? _punctured_cells[cid] : _punctured_cells1[cid];
    c.SetChirality(fid, chirality * fchirality);
  }
}

void VortexExtractor::AddPuncturedEdge(EdgeIdType id, ChiralityType chirality, double t)
{
  // edge
  PuncturedEdge pe;

  pe.chirality = chirality;
  pe.t = t;

  _punctured_edges[id] = pe;

  // vface
  const MeshGraph *mg = _dataset->MeshGraph();
  const CEdge &edge = mg->Edge(id);
  for (int i=0; i<edge.contained_faces.size(); i++) {
    int echirality = edge.contained_faces_chirality[i];
    int eid = edge.contained_faces_eid[i]; 
    PuncturedCell &vc = _punctured_vcells[edge.contained_faces[i]];
    vc.SetChirality(eid+2, chirality * echirality);
  }
}
  
bool VortexExtractor::FindSpaceTimeEdgeZero(const double re[], const double im[], double &t) const
{
  double p[2];
  if (!find_zero_unit_quad_bilinear(re, im, p))
    if (!find_zero_unit_quad_barycentric(re, im, p))
      return false;

  t = p[1];
  return true;
}

void VortexExtractor::RelateOverTime()
{
  fprintf(stderr, "Relating over time, #pf0=%ld, #pf1=%ld, #pe=%ld\n", 
      _punctured_faces.size(), _punctured_faces1.size(), _punctured_edges.size());
  const MeshGraph *mg = _dataset->MeshGraph();

  _related_faces.clear();

  for (std::map<FaceIdType, PuncturedFace>::iterator it = _punctured_faces.begin(); 
       it != _punctured_faces.end(); it ++) 
  {
    // fprintf(stderr, "fid=%u\n", it->first);

    std::vector<FaceIdType> related;
    
    std::list<FaceIdType> faces_to_visit;
    std::list<int> faces_to_visit_chirality; // face chirality
    std::list<double> faces_to_visit_time;
    std::set<FaceIdType> faces_visited;
    std::set<EdgeIdType> edges_visited;
    
    faces_to_visit.push_back(it->first);
    faces_to_visit_chirality.push_back(it->second.chirality);
    faces_to_visit_time.push_back(0);
      
    std::map<FaceIdType, FaceIdType> parent_map;
    std::map<FaceIdType, std::tuple<EdgeIdType, double> > parent_edge_map;

    while (!faces_to_visit.empty()) {
      FaceIdType current = faces_to_visit.front();
      int current_chirality = faces_to_visit_chirality.front();
      double current_time = faces_to_visit_time.front();

      faces_to_visit.pop_front();
      faces_to_visit_chirality.pop_front();
      faces_to_visit_time.pop_front();

      faces_visited.insert(current);
           
      // if (_punctured_faces1[current].chirality != 0 && _punctured_faces1[current].chirality != current_chirality)
      //   fprintf(stderr, "chi not match: current_chi=%d, face_chi=%d\n", current_chirality, _punctured_faces1[current].chirality);

      if (_punctured_faces1.find(current) != _punctured_faces1.end() && 
          _punctured_faces1[current].chirality == current_chirality) 
      {
        related.push_back(current);
#if 0 // for debug purposes, print traverse history
        std::list<FaceIdType> history_faces;
        std::list<std::tuple<EdgeIdType, double> > history_edges;

        history_faces.push_back(current);
        std::map<FaceIdType, FaceIdType>::iterator it = parent_map.find(current);
        while (it != parent_map.end()) {
          history_edges.push_front(parent_edge_map[it->first]);
          history_faces.push_front(it->second);
          it = parent_map.find(it->second);
        }

        if (history_faces.size() > 1) {
          int i=0;
          std::list<std::tuple<EdgeIdType, double> >::iterator it1 = history_edges.begin();
          for (std::list<FaceIdType>::iterator it = history_faces.begin(); 
               it != history_faces.end(); it ++) 
          {
            if (i<history_faces.size()-1) 
              fprintf(stderr, "%u->(%u, %.2f)->", *it, std::get<0>(*it1), std::get<1>(*it1));
            else 
              fprintf(stderr, "%u\n", *it);
            i++;
            it1++;
          }
        }
#endif
      }

      // add neighbors
      const CFace &face = mg->Face(current);
      for (int i=0; i<face.edges.size(); i++) {
        // find punctured edges
        EdgeIdType e = face.edges[i];
        if (_punctured_edges.find(e) != _punctured_edges.end() && 
            edges_visited.find(e) == edges_visited.end())
        {
          edges_visited.insert(e);
          
          const CEdge &edge = mg->Edge(e);
          const PuncturedEdge& pe = _punctured_edges[e];
          // if (current_time >= pe.t) continue; // time ascending order
            
          int echirality = face.edges_chirality[i] * pe.chirality;
          if (current_chirality == echirality) { // this check is right
            /// find neighbor faces who chontain this edge
            for (int j=0; j<edge.contained_faces.size(); j++) {
              if (faces_visited.find(edge.contained_faces[j]) == faces_visited.end()) { // not found in visited faces
                // fprintf(stderr, "fid0=%u, chi=%d, found edge eid=%u, face_edge_chi=%d, edge_chi=%d, edge_contained_face=%u, edge_contained_face_chi=%d\n", 
                //     current, current_chirality, e, face.edges_chirality[i], pe.chirality,  
                //     edge.contained_faces[j], edge.contained_faces_chirality[j]);
                faces_to_visit.push_front(edge.contained_faces[j]);
                // faces_to_visit_chirality.push_front(-edge.contained_faces_chirality[j] * current_chirality);
                faces_to_visit_chirality.push_front(-edge.contained_faces_chirality[j] * pe.chirality); 
                // faces_to_visit_chirality.push_front(edge.contained_faces_chirality[j]);
                faces_to_visit_time.push_front(pe.t);
                parent_map[edge.contained_faces[j]] = current;
                parent_edge_map[edge.contained_faces[j]] = std::make_tuple(e, pe.t);
              }
            }
          }
        }
      }
    }

    _related_faces[it->first] = related;

#if 0
    // if (1) {
    if (!(related.size() == 1 && it->first == related[0])) { // non-ordinary
      fprintf(stderr, "fid=%u, related={", it->first);
      for (int i=0; i<related.size(); i++)
        if (i<related.size()-1)
          fprintf(stderr, "%u, ", related[i]);
        else 
          fprintf(stderr, "%u", related[i]);
      fprintf(stderr, "}\n");
    }
#endif
  }
}

void VortexExtractor::TraceVirtualCells()
{
  int n_self = 0, n_pure = 0, n_cross = 0, n_invalid = 0;
  for (std::map<FaceIdType, PuncturedCell>::iterator it = _punctured_vcells.begin(); it != _punctured_vcells.end(); it ++) 
  {
    int c[5];
    for (int i=0; i<5; i++) 
      c[i] = it->second.Chirality(i);
    
    bool punctured = c[0] || c[1] || c[2] || c[3] || c[4];
    bool pure = punctured && !c[0] && !c[1];
    bool self = c[0] && c[1];
    bool cross = (c[0] || c[1]) && (c[2] || c[3] || c[4]);
    int sum = c[0] + c[1] + c[2] + c[3] + c[4];

    if (sum != 0) n_invalid ++;
    if (pure) n_pure ++;
    if (self) n_self ++;
    if (cross) n_cross ++;

    if (sum != 0) 
      fprintf(stderr, "--SPECIAL:\n");
    fprintf(stderr, "%d\t%d\t%d\t%d\t%d\n", c[0], c[1], c[2], c[3], c[4]);
  }
  fprintf(stderr, "n_self=%d, n_pure=%d, n_cross=%d, n_invalid=%d\n", 
      n_self, n_pure, n_cross, n_invalid);
}

void VortexExtractor::TraceOverSpace(int slot)
{
  std::vector<VortexObject> &vobjs = 
    slot == 0 ? _vortex_objects : _vortex_objects1;
  std::vector<VortexLine> &vlines = 
    slot == 0 ? _vortex_lines : _vortex_lines1;
  std::map<CellIdType, PuncturedCell> &pcs = 
    slot == 0 ? _punctured_cells : _punctured_cells1;
  std::map<FaceIdType, PuncturedFace> &pfs =
    slot == 0 ? _punctured_faces : _punctured_faces1;
  const MeshGraph *mg = _dataset->MeshGraph();
  
  fprintf(stderr, "tracing over space, #pcs=%ld, #pfs=%ld.\n", pcs.size(), pfs.size());
  
  vobjs.clear();
  while (!pcs.empty()) {
    /// 1. sort punctured cells into connected ordinary/special ones
    std::list<CellIdType> to_visit;
    std::set<CellIdType> visited;
    
    to_visit.push_back(pcs.begin()->first); 
    std::map<CellIdType, PuncturedCell> ordinary_pcells, special_pcells;

    while (!to_visit.empty()) { // depth-first search
      CellIdType c = to_visit.front();
      to_visit.pop_front();
    
      const PuncturedCell &pcell = pcs[c];
      const CCell &cell = mg->Cell(c);

      if (pcell.IsSpecial()) 
        special_pcells[c] = pcell;
      else 
        ordinary_pcells[c] = pcell;
      visited.insert(c);

      for (int i=0; i<cell.neighbor_cells.size(); i++) {
        CellIdType c1 = cell.neighbor_cells[i];
        if (c1 != UINT_MAX                            // valid neighbor cell
            && pcell.Chirality(i) != 0                // corresponding face punctured
            && pcs.find(c1) != pcs.end()              // neighbor cell punctured
            && visited.find(c1) == visited.end())     // not visited
        {
          to_visit.push_back(c1);
        }
      }
    }
  
    for (std::set<CellIdType>::iterator it = visited.begin(); it != visited.end(); it ++)
      pcs.erase(*it);
    visited.clear();

    // fprintf(stderr, "#ordinary=%ld, #special=%ld\n", ordinary_pcells.size(), special_pcells.size());
    if (special_pcells.size()>0) 
      fprintf(stderr, "SPECIAL\n");

    /// 2. trace vortex lines
    VortexObject vobj; 
    
    /// 2.2 trace backward and forward
    while (!ordinary_pcells.empty()) {
      std::list<FaceIdType> trace;
      CellIdType seed = ordinary_pcells.begin()->first;
      
      visited.clear();

      // trace forward (chirality == 1)
      CellIdType c = seed;
      bool traced; 
      while (1) {
        traced = false;
        if (ordinary_pcells.find(c) == ordinary_pcells.end() 
            || visited.find(c) != visited.end())
          break;

        const PuncturedCell &pcell = ordinary_pcells[c];
        const CCell &cell = mg->Cell(c);

        // std::vector<ElemIdType> neighbors = _dataset->GetNeighborIds(it->first); 
        for (int i=0; i<cell.neighbor_cells.size(); i++) {
          if (pcell.Chirality(i) == 1) {
            visited.insert(c);
            // if (cell.neighbor_cells[i] != UINT_MAX  // not boundary
            //     && special_pcells.find(cell.neighbor_cells[i]) == special_pcells.end()) // not special
            if (special_pcells.find(cell.neighbor_cells[i]) == special_pcells.end()) // not special
            {
              FaceIdType f = cell.faces[i];
              vobj.faces.insert(f);
              trace.push_back(f);
              c = cell.neighbor_cells[i]; 
              traced = true;
            }
          }
        }
        if (!traced) break;
      }

      // trace backward (chirality == -1)
      visited.erase(seed);
      c = seed;
      while (1) {
        traced = false;
        if (ordinary_pcells.find(c) == ordinary_pcells.end() 
            || visited.find(c) != visited.end())
          break;

        const PuncturedCell &pcell = ordinary_pcells[c];
        const CCell &cell = mg->Cell(c);

        // std::vector<ElemIdType> neighbors = _dataset->GetNeighborIds(it->first); 
        for (int i=0; i<cell.neighbor_cells.size(); i++) {
          if (pcell.Chirality(i) == -1) {
            visited.insert(c);
            // if (cell.neighbor_cells[i] != UINT_MAX  // not boundary
            //     && special_pcells.find(cell.neighbor_cells[i]) == special_pcells.end()) // not special
            if (special_pcells.find(cell.neighbor_cells[i]) == special_pcells.end()) // not special
            {
              FaceIdType f = cell.faces[i];
              vobj.faces.insert(f);
              trace.push_front(f);
              c = cell.neighbor_cells[i]; 
              traced = true;
            }
          }
        }
        if (!traced) break;
      }
      
      visited.insert(seed);
      
      for (std::set<CellIdType>::iterator it = visited.begin(); it != visited.end(); it ++)
        ordinary_pcells.erase(*it);
      visited.clear();

      vobj.traces.push_back(trace);
    }

    // vobj.id = NewVortexId();
    vobj.id = vobjs.size();  // local (time) id
    vobjs.push_back(vobj);
  }

  fprintf(stderr, "#vortex_objs=%ld\n", vobjs.size());
}

void VortexExtractor::VortexObjectsToVortexLines(
    const std::map<FaceIdType, PuncturedFace>& pfs, 
    const std::vector<VortexObject>& vobjs, 
    std::vector<VortexLine>& vlines, bool bezier)
{
  for (int i=0; i<vobjs.size(); i++) {
    const VortexObject& vobj = vobjs[i];
    VortexLine line;
    line.id = vobj.id;
    line.gid = vobj.gid;
    line.timestep = vobj.timestep;
    
    for (int j=0; j<vobj.traces.size(); j++) {
      const std::list<FaceIdType> &trace = vobj.traces[j];
      for (std::list<FaceIdType>::const_iterator it = trace.begin(); it != trace.end(); it ++) {
        const std::map<FaceIdType, PuncturedFace>::const_iterator it1 = pfs.find(*it);
        assert(it1 != pfs.end());
        // if (it1 == pfs.end()) continue;
        const PuncturedFace& pf = it1->second;
        line.push_back(pf.pos[0]);
        line.push_back(pf.pos[1]);
        line.push_back(pf.pos[2]);
        // fprintf(stderr, "{%f, %f, %f}\n", pf.pos[0], pf.pos[1], pf.pos[2]);
      }
    }

    if (bezier) {
      line.Flattern(Dataset()->Origins(), Dataset()->Lengths());
      line.ToBezier();
    }

    vlines.push_back(line);
  }
}

int VortexExtractor::NewGlobalVortexId()
{
  static int id = 0;
  return id++;
}

// only relate ids
void VortexExtractor::TraceOverTime()
{
  const int n0 = _vortex_objects.size(), 
            n1 = _vortex_objects1.size();
  // VortexTransitionMatrix &tm = _vortex_transition[_dataset->TimeStep(0)]; 
  VortexTransitionMatrix tm(_dataset->TimeStep(0), _dataset->TimeStep(1), n0, n1);

  RelateOverTime();

  for (int i=0; i<n0; i++) {
    for (int j=0; j<n1; j++) {
      for (std::set<FaceIdType>::iterator it = _vortex_objects[i].faces.begin(); 
          it != _vortex_objects[i].faces.end(); it ++) 
      {
        const std::vector<FaceIdType> &related = _related_faces[*it];
        for (int k=0; k<related.size(); k++) {
          if (_vortex_objects1[j].faces.find(related[k]) != _vortex_objects1[j].faces.end()) {
            // if (i != j)
            //   fprintf(stderr, "vid=%d --> vid=%d, fid0=%u, fid1=%u\n", i, j, *it, related[k]);
            tm(i, j) ++;
            goto next;
          }
        }
      }
next: 
      continue;
    }
  }

  if (_archive) tm.SaveToFile(Dataset()->DataName(), Dataset()->TimeStep(0), Dataset()->TimeStep(1));
  _vortex_transition.AddMatrix(tm);

#if 0
  std::vector<int> ids0(n0), ids1(n1);
  if (_num_global_vortices == 0) 
    for (int i=0; i<n0; i++) 
      ids0[i] = NewGlobalVortexId();

  tm.RenumberIds(ids0, ids1, std::bind(&VortexExtractor::NewGlobalVortexId, this));
#endif

#if 0
  // detection from row sum. possible events: death, split, 
  for (int i=0; i<n0; i++) {
    int sum = 0;
    int j1;
    for (int j=0; j<n1; j++) {
      if (tm(i, j)) {
        sum ++;
        j1 = j;
      }
    }
    if (sum == 1) { // link the two
      _vortex_objects1[j1].id = _vortex_objects[i].id;
    } else {
      _vortex_objects1[j1].id = NewGlobalVortexId();
      fprintf(stderr, "special event detected, vid0=%d\n", i);
    }
  }
#endif

  // detection from column sum. possible events: birth, merge
#if 0
  for (int j=0; j<n1; j++) {
    int sum = 0;

  }
#endif

#if 0 // debug output
  for (int i=0; i<n0; i++) {
    fprintf(stderr, "vid=%d\n", _vortex_objects[i].id);
    for (int j=0; j<n1; j++) {
      if (j<n1-1) fprintf(stderr, "%d, ", match[i*n1+j]);
      else fprintf(stderr, "%d\n", match[i*n1+j]);
    }
  }
#endif
}

void VortexExtractor::AnalyzeTransition()
{

}

void VortexExtractor::RotateTimeSteps()
{
  _punctured_faces.clear();
  _punctured_cells.clear();
  _vortex_objects.clear();
  _vortex_lines.clear();

  _punctured_edges.clear();
  _punctured_vcells.clear();
  _related_faces.clear();

  _punctured_faces.swap( _punctured_faces1 );
  _punctured_cells.swap( _punctured_cells1 );
  _vortex_objects.swap( _vortex_objects1 );
  _vortex_lines.swap( _vortex_lines1 );
}

void VortexExtractor::ExtractFaces(int slot) 
{
  const MeshGraph *mg = _dataset->MeshGraph();

  if (!LoadPuncturedFaces(slot)) {
    for (FaceIdType i=0; i<mg->NFaces(); i++) 
      ExtractFace(i, slot);
    if (_archive) SavePuncturedFaces(slot);
  }
}

void VortexExtractor::ExtractEdges() 
{
  const MeshGraph *mg = _dataset->MeshGraph();
 
  if (!LoadPuncturedEdges()) {
    for (EdgeIdType i=0; i<mg->NEdges(); i++) 
      ExtractSpaceTimeEdge(i);
    if (_archive) SavePuncturedEdges();
  }
}

void VortexExtractor::ExtractSpaceTimeEdge(EdgeIdType id)
{
  const GLDataset *ds = (GLDataset*)_dataset;
  const CEdge& e = _dataset->MeshGraph()->Edge(id);

  if (!e.Valid()) {
    // fprintf(stderr, "invalid edge\n");
    return;
  }

  double X[4][3], A[4][3], re[3], im[3];
  ds->GetSpaceTimeEdgeValues(e, X, A, re, im);

  double rho[4], phi[4];
  for (int i=0; i<4; i++) {
    rho[i] = sqrt(re[i]*re[i] + im[i]*im[i]);
    phi[i] = atan2(im[i], re[i]);
  }

  const double dt = ds->Time(1) - ds->Time(0);
  double li[4] = {
    ds->LineIntegral(X[0], X[1], A[0], A[1]), 
    0, 
    // 0.5 * (ds->Kex(1) + ds->Kex(0)) * dt, 
    ds->LineIntegral(X[1], X[0], A[2], A[3]), 
    0}; 
    // -0.5 * (ds->Kex(1) + ds->Kex(0)) * dt};
  double qp[4] = {
    ds->QP(X[0], X[1]), 0, 
    ds->QP(X[1], X[0]), 0};
  double delta[4] = {
    phi[1] - phi[0],
    phi[2] - phi[1],
    phi[3] - phi[2],
    phi[0] - phi[3]
  };

  for (int i=0; i<4; i++) 
    if (_gauge) delta[i] = mod2pi1(delta[i] - li[i] + qp[i]);
    else delta[i] = mod2pi1(delta[i] + qp[i]);

  double phase_shift = -(delta[0] + delta[1] + delta[2] + delta[3]);
  double critera = phase_shift / (2*M_PI);

  ChiralityType chirality;
  if (critera > 0.5) chirality = 1; 
  else if (critera < -0.5) chirality = -1;
  else return;

  // gauge transformation
  if (_gauge) {
    for (int i=1; i<4; i++) {
      phi[i] = phi[i-1] + delta[i-1];
      re[i] = rho[i] * cos(phi[i]); 
      im[i] = rho[i] * sin(phi[i]);
    }
  }

  // find zero
  double t = 0;
  if (FindSpaceTimeEdgeZero(re, im, t)) {
    // fprintf(stderr, "punctured edge: eid=%u, chirality=%d, t=%f\n", 
    //     id, chirality, t);
    AddPuncturedEdge(id, chirality, t);
  } else {
    fprintf(stderr, "WARNING: zero time not found.\n");
    AddPuncturedEdge(id, chirality, NAN);
  }
}

void VortexExtractor::ExtractFace(FaceIdType id, int slot)
{
  const GLDataset *ds = (GLDataset*)_dataset;
  const int nnodes = ds->NrNodesPerFace();
  const CFace& f = ds->MeshGraph()->Face(id);

  if (!f.Valid()) return;

  double X[nnodes][3], A[nnodes][3], re[nnodes], im[nnodes];
  ds->GetFaceValues(f, slot, X, A, re, im);
    
  // compute rho & phi
  double rho[nnodes], phi[nnodes];
  for (int i=0; i<nnodes; i++) {
    rho[i] = sqrt(re[i]*re[i] + im[i]*im[i]);
    phi[i] = atan2(im[i], re[i]);
  }

  // calculating phase shift
  double delta[nnodes], phase_shift = 0;
  for (int i=0; i<nnodes; i++) {
    int j = (i+1) % nnodes;
    delta[i] = phi[j] - phi[i]; 
    double li = ds->LineIntegral(X[i], X[j], A[i], A[j]), 
           qp = ds->QP(X[i], X[j]);
    if (_gauge) 
      // delta[i] = mod2pi1(delta[i] - li + qp);
      delta[i] = mod2pi1(delta[i] - li + qp);
    else 
      delta[i] = mod2pi1(delta[i] + qp);
    phase_shift -= delta[i];
  }

  // check if punctured
  double critera = phase_shift / (2*M_PI);
  if (fabs(critera)<0.5) return; // not punctured

  // chirality
  ChiralityType chirality = critera>0 ? 1 : -1;

  // gauge transformation
  if (_gauge) { 
    for (int i=1; i<nnodes; i++) {
      phi[i] = phi[i-1] + delta[i-1];
      re[i] = rho[i] * cos(phi[i]); 
      im[i] = rho[i] * sin(phi[i]);
    }
  }

  // find zero
  double pos[3];
  if (FindFaceZero(X, re, im, pos)) {
    AddPuncturedFace(id, slot, chirality, pos);
    // fprintf(stderr, "pos={%f, %f, %f}, chi=%d\n", pos[0], pos[1], pos[2], chirality);
  } else {
    fprintf(stderr, "WARNING: punctured but singularity not found.\n");
    pos[0] = pos[1] = pos[2] = NAN;
    AddPuncturedFace(id, slot, chirality, pos);
  }
}
