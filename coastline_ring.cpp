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

void CoastlineRing::setup_positions(posmap_t& posmap) {
    for (Osmium::OSM::WayNodeList::iterator it = m_way_node_list.begin(); it != m_way_node_list.end(); ++it) {
        posmap.insert(std::make_pair(it->ref(), &(it->position())));
    }
}

void CoastlineRing::add_at_front(const shared_ptr<Osmium::OSM::Way>& way) {
    assert(first_node_id() == way->get_last_node_id());
    m_way_node_list.insert(m_way_node_list.begin(), way->nodes().begin(), way->nodes().end()-1);

    update_ring_id(way->id());
    m_nways++;
}

void CoastlineRing::add_at_end(const shared_ptr<Osmium::OSM::Way>& way) {
    assert(last_node_id() == way->get_first_node_id());
    m_way_node_list.insert(m_way_node_list.end(), way->nodes().begin()+1, way->nodes().end());

    update_ring_id(way->id());
    m_nways++;
}

void CoastlineRing::join(const CoastlineRing& other) {
    assert(last_node_id() == other.first_node_id());
    m_way_node_list.insert(m_way_node_list.end(), other.m_way_node_list.begin()+1, other.m_way_node_list.end());

    update_ring_id(other.ring_id());
    m_nways += other.m_nways;
}

void CoastlineRing::join_over_gap(const CoastlineRing& other) {
    if (last_position() != other.first_position()) {
        m_way_node_list.add(other.m_way_node_list.front());
    }

    m_way_node_list.insert(m_way_node_list.end(), other.m_way_node_list.begin()+1, other.m_way_node_list.end());

    update_ring_id(other.ring_id());
    m_nways += other.m_nways;
    m_fixed = true;
}

void CoastlineRing::close_ring() {
    if (first_position() != last_position()) {
        m_way_node_list.add(m_way_node_list.front());
    }
    m_fixed = true;
}

OGRPolygon* CoastlineRing::ogr_polygon() const {
    const Osmium::Geometry::Polygon geom(m_way_node_list, true);
    return geom.create_ogr_geometry();
}

OGRLineString* CoastlineRing::ogr_linestring() const {
    const Osmium::Geometry::LineString geom(m_way_node_list, true);
    return geom.create_ogr_geometry();
}

OGRPoint* CoastlineRing::ogr_first_point() const {
    const Osmium::OSM::WayNode& wn = m_way_node_list.front();
    return new OGRPoint(wn.lon(), wn.lat());
}

OGRPoint* CoastlineRing::ogr_last_point() const {
    const Osmium::OSM::WayNode& wn = m_way_node_list.back();
    return new OGRPoint(wn.lon(), wn.lat());
}

// Pythagoras doesn't work on a round earth but that is ok here, we only need a
// rough measure anyway
double CoastlineRing::distance_to_start_position(Osmium::OSM::Position pos) const {
    Osmium::OSM::Position p = m_way_node_list.front().position();
    return (pos.lon() - p.lon()) * (pos.lon() - p.lon()) + (pos.lat() - p.lat()) * (pos.lat() - p.lat());
}

std::ostream& operator<<(std::ostream& out, CoastlineRing& cp) {
    out << "CoastlineRing(ring_id=" << cp.ring_id() << ", nways=" << cp.nways() << ", npoints=" << cp.npoints() << ", first_node_id=" << cp.first_node_id() << ", last_node_id=" << cp.last_node_id();
    if (cp.is_closed()) {
        out << " [CLOSED]";
    }
    out << ")";
    return out;
}

