// traffic.cpp  - Traffic module for swift<->FG connection
//
// Copyright (C) 2019 - swift Project Community / Contributors (http://swift-project.org/)
// Adapted to Flightgear by Lars Toenning <dev@ltoenning.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

//! \cond PRIVATE

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "traffic.h"
#include "SwiftAircraftManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iostream>


// clazy:excludeall=reserve-candidates

namespace FGSwiftBus {

CTraffic::CTraffic()
{
    acm = new FGSwiftAircraftManager();
    SG_LOG(SG_NETWORK, SG_INFO, "FGSwiftBus Traffic started");
}

CTraffic::~CTraffic()
{
    cleanup();
    SG_LOG(SG_NETWORK, SG_INFO, "FGSwiftBus Traffic stopped");
}

const std::string& CTraffic::InterfaceName()
{
    static std::string s(FGSWIFTBUS_TRAFFIC_INTERFACENAME);
    return s;
}

const std::string& CTraffic::ObjectPath()
{
    static std::string s(FGSWIFTBUS_TRAFFIC_OBJECTPATH);
    return s;
}

void CTraffic::planeLoaded(void* id, bool succeeded, void* self)
{
    auto* traffic = static_cast<CTraffic*>(self);
    auto  planeIt = traffic->m_planesById.find(id);
    if (planeIt == traffic->m_planesById.end()) { return; }

    if (succeeded) {
        traffic->emitPlaneAdded(planeIt->second->callsign);
    } else {
        traffic->emitPlaneAddingFailed(planeIt->second->callsign);
    }
}

bool CTraffic::initialize()
{
    m_initialized = true;

    m_enabled = true;

    return true;
}

void CTraffic::emitSimFrame()
{
    if (m_emitSimFrame) { sendDBusSignal("simFrame"); }
    m_emitSimFrame = !m_emitSimFrame;
}

void CTraffic::emitPlaneAdded(const std::string& callsign)
{
    CDBusMessage signalPlaneAdded = CDBusMessage::createSignal(FGSWIFTBUS_TRAFFIC_OBJECTPATH, FGSWIFTBUS_TRAFFIC_INTERFACENAME, "remoteAircraftAdded");
    signalPlaneAdded.beginArgumentWrite();
    signalPlaneAdded.appendArgument(callsign);
    sendDBusMessage(signalPlaneAdded);
}

void CTraffic::emitPlaneAddingFailed(const std::string& callsign)
{
    CDBusMessage signalPlaneAddingFailed = CDBusMessage::createSignal(FGSWIFTBUS_TRAFFIC_OBJECTPATH, FGSWIFTBUS_TRAFFIC_INTERFACENAME, "remoteAircraftAddingFailed");
    signalPlaneAddingFailed.beginArgumentWrite();
    signalPlaneAddingFailed.appendArgument(callsign);
    sendDBusMessage(signalPlaneAddingFailed);
}

void CTraffic::cleanup()
{
    acm->removeAllPlanes();
}

void CTraffic::removeAllPlanes()
{
    acm->removeAllPlanes();
}

void CTraffic::dbusDisconnectedHandler()
{
    acm->removeAllPlanes();
}

const char* introspection_traffic = DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE;

DBusHandlerResult CTraffic::dbusMessageHandler(const CDBusMessage& message_)
{
    CDBusMessage        message(message_);
    const std::string   sender     = message.getSender();
    const dbus_uint32_t serial     = message.getSerial();
    const bool          wantsReply = message.wantsReply();
    std::string         test       = message.getMethodName();

    if (message.getInterfaceName() == DBUS_INTERFACE_INTROSPECTABLE) {
        if (message.getMethodName() == "Introspect") {
            sendDBusReply(sender, serial, introspection_traffic);
        }
    } else if (message.getInterfaceName() == FGSWIFTBUS_TRAFFIC_INTERFACENAME) {
        if (message.getMethodName() == "acquireMultiplayerPlanes") {
            queueDBusCall([=]() {
                std::string  owner;
                bool         acquired = true;
                CDBusMessage reply    = CDBusMessage::createReply(sender, serial);
                reply.beginArgumentWrite();
                reply.appendArgument(acquired);
                reply.appendArgument(owner);
                sendDBusMessage(reply);
            });
        } else if (message.getMethodName() == "initialize") {
            sendDBusReply(sender, serial, initialize());
        } else if (message.getMethodName() == "cleanup") {
            maybeSendEmptyDBusReply(wantsReply, sender, serial);
            queueDBusCall([=]() {
                cleanup();
            });
        } else if (message.getMethodName() == "addPlane") {
            maybeSendEmptyDBusReply(wantsReply, sender, serial);
            std::string callsign;
            std::string modelName;
            std::string aircraftIcao;
            std::string airlineIcao;
            std::string livery;
            message.beginArgumentRead();
            message.getArgument(callsign);
            message.getArgument(modelName);
            message.getArgument(aircraftIcao);
            message.getArgument(airlineIcao);
            message.getArgument(livery);

            queueDBusCall([=]() {
                if (acm->addPlane(callsign, modelName)) {
                    emitPlaneAdded(callsign);
                }
            });
        } else if (message.getMethodName() == "removePlane") {
            maybeSendEmptyDBusReply(wantsReply, sender, serial);
            std::string callsign;
            message.beginArgumentRead();
            message.getArgument(callsign);
            queueDBusCall([=]() {
                acm->removePlane(callsign);
            });
        } else if (message.getMethodName() == "removeAllPlanes") {
            maybeSendEmptyDBusReply(wantsReply, sender, serial);
            queueDBusCall([=]() {
                acm->removeAllPlanes();
            });
        } else if (message.getMethodName() == "setPlanesPositions") {
            maybeSendEmptyDBusReply(wantsReply, sender, serial);
            std::vector<std::string> callsigns;
            std::vector<double>      latitudes;
            std::vector<double>      longitudes;
            std::vector<double>      altitudes;
            std::vector<double>      pitches;
            std::vector<double>      rolls;
            std::vector<double>      headings;
            std::vector<bool>        onGrounds;
            message.beginArgumentRead();
            message.getArgument(callsigns);
            message.getArgument(latitudes);
            message.getArgument(longitudes);
            message.getArgument(altitudes);
            message.getArgument(pitches);
            message.getArgument(rolls);
            message.getArgument(headings);
            message.getArgument(onGrounds);
            queueDBusCall([=]() {
                std::vector<SGGeod> positions;
                std::vector<SGVec3d> orientations;
                for (int i = 0; i < latitudes.size(); i++) {
                    SGGeod newPos;
                    newPos.setLatitudeDeg(latitudes.at(i));
                    newPos.setLongitudeDeg(longitudes.at(i));
                    newPos.setElevationFt(altitudes.at(i));
                    SGVec3d vec(pitches.at(i), rolls.at(i), headings.at(i));

                    positions.push_back(newPos);
                    orientations.push_back(vec);
                }
                acm->updatePlanes(callsigns, positions, orientations, onGrounds);
            });
        } else if (message.getMethodName() == "getRemoteAircraftData") {
            std::vector<std::string> requestedcallsigns;
            message.beginArgumentRead();
            message.getArgument(requestedcallsigns);
            queueDBusCall([=]() {
                std::vector<std::string> callsigns = requestedcallsigns;
                std::vector<double>      latitudesDeg;
                std::vector<double>      longitudesDeg;
                std::vector<double>      elevationsM;
                std::vector<double>      verticalOffsets;
                acm->getRemoteAircraftData(callsigns, latitudesDeg, longitudesDeg, elevationsM, verticalOffsets);
                CDBusMessage reply = CDBusMessage::createReply(sender, serial);
                reply.beginArgumentWrite();
                reply.appendArgument(callsigns);
                reply.appendArgument(latitudesDeg);
                reply.appendArgument(longitudesDeg);
                reply.appendArgument(elevationsM);
                reply.appendArgument(verticalOffsets);
                sendDBusMessage(reply);
            });
        } else {
            // Unknown message. Tell DBus that we cannot handle it
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

int CTraffic::process()
{
    invokeQueuedDBusCalls();
    return 1;
}

//! memcmp function which ignores the header ("size" member) and compares only the payload (the rest of the struct)
template <typename T>
int memcmpPayload(T* dst, T* src)
{
    return std::memcmp(reinterpret_cast<char*>(dst) + sizeof(dst->size),
                       reinterpret_cast<char*>(src) + sizeof(src->size),
                       sizeof(*dst) - sizeof(dst->size));
}
} // namespace FGSwiftBus

//! \endcond
