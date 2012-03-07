/*

  Copyright 2012 Jochen Topf <jochen@topf.org>.

  This file is part of OSMCoastline.

  OSMCoastline is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OSMCoastline is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OSMCoastline.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <osmium/geometry/polygon.hpp>
#include <osmium/geometry/linestring.hpp>

#include "coastline_ring.hpp"

void CoastlineRing::join(const CoastlineRing& other) {
    assert(last_node_id() == other.first_node_id());
    m_way_node_list.insert(m_way_node_list.end(), other.m_way_node_list.begin()+1, other.m_way_node_list.end());

    m_last_node_id = other.last_node_id();
    if (other.min_way_id() < m_min_way_id) {
        m_min_way_id = other.min_way_id();
    }
    m_nways += other.m_nways;
}

void CoastlineRing::add_at_end(const shared_ptr<Osmium::OSM::Way>& way) {
    assert(last_node_id() == way->get_first_node_id());
    m_way_node_list.insert(m_way_node_list.end(), way->nodes().begin()+1, way->nodes().end());

    m_last_node_id = way->get_last_node_id();
    if (way->id() < m_min_way_id) {
        m_min_way_id = way->id();
    }
    m_nways++;
}

void CoastlineRing::add_at_front(const shared_ptr<Osmium::OSM::Way>& way) {
    assert(first_node_id() == way->get_last_node_id());
    m_way_node_list.insert(m_way_node_list.begin(), way->nodes().begin(), way->nodes().end()-1);

    m_first_node_id = way->get_first_node_id();
    if (way->id() < m_min_way_id) {
        m_min_way_id = way->id();
    }
    m_nways++;
}

void CoastlineRing::setup_positions(posmap_t& posmap) {
    for (Osmium::OSM::WayNodeList::iterator it = m_way_node_list.begin(); it != m_way_node_list.end(); ++it) {
        posmap.insert(std::make_pair(it->ref(), &(it->position())));
    }
}

OGRPolygon* CoastlineRing::ogr_polygon() const {
    Osmium::Geometry::Polygon geom(m_way_node_list, true);
    return geom.create_ogr_geometry();
}

OGRLineString* CoastlineRing::ogr_linestring() const {
    Osmium::Geometry::LineString geom(m_way_node_list, true);
    return geom.create_ogr_geometry();
}

OGRPoint* CoastlineRing::ogr_first_point() const {
    const Osmium::OSM::WayNode& wn = m_way_node_list.back();
    return new OGRPoint(wn.lon(), wn.lat());
}

OGRPoint* CoastlineRing::ogr_last_point() const {
    const Osmium::OSM::WayNode& wn = m_way_node_list.front();
    return new OGRPoint(wn.lon(), wn.lat());
}

std::ostream& operator<<(std::ostream& out, CoastlineRing& cp) {
    out << "CoastlineRing(id=" << cp.min_way_id() << ", nways=" << cp.nways() << ", npoints=" << cp.npoints() << ", first_node_id=" << cp.first_node_id() << ", last_node_id=" << cp.last_node_id();
    if (cp.is_closed()) {
        out << " [CLOSED]";
    }
    out << ")";
    return out;
}

