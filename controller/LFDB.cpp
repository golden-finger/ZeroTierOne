/*
 * ZeroTier One - Network Virtualization Everywhere
 * Copyright (C) 2011-2019  ZeroTier, Inc.  https://www.zerotier.com/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * --
 *
 * You can be released from the requirements of the license by purchasing
 * a commercial license. Buying such a license is mandatory as soon as you
 * develop commercial closed-source software that incorporates or links
 * directly against ZeroTier software without disclosing the source code
 * of your own application.
 */

#include "LFDB.hpp"

#include <thread>
#include <chrono>
#include <iostream>
#include <sstream>

#include "../osdep/OSUtils.hpp"
#include "../ext/cpp-httplib/httplib.h"

namespace ZeroTier
{

LFDB::LFDB(const Identity &myId,const char *path,const char *lfOwnerPrivate,const char *lfOwnerPublic,const char *lfNodeHost,int lfNodePort,bool storeOnlineState) :
	DB(myId,path),
	_myId(myId),
	_lfOwnerPrivate((lfOwnerPrivate) ? lfOwnerPrivate : ""),
	_lfOwnerPublic((lfOwnerPublic) ? lfOwnerPublic : ""),
	_lfNodeHost((lfNodeHost) ? lfNodeHost : "127.0.0.1"),
	_lfNodePort(((lfNodePort > 0)&&(lfNodePort < 65536)) ? lfNodePort : 9980),
	_running(true),
	_ready(false),
	_storeOnlineState(storeOnlineState)
{
	_syncThread = std::thread([this]() {
		char controllerAddress[24];
		const uint64_t controllerAddressInt = _myId.address().toInt();
		_myId.address().toString(controllerAddress);
		std::string networksSelectorName("com.zerotier.controller.lfdb:"); networksSelectorName.append(controllerAddress); networksSelectorName.append("/network");
		std::string membersSelectorName("com.zerotier.controller.lfdb:"); membersSelectorName.append(controllerAddress); membersSelectorName.append("/member");

		// LF record masking key is the first 32 bytes of SHA512(controller private key) in hex,
		// hiding record values from anything but the controller or someone who has its key.
		uint8_t sha512pk[64];
		_myId.sha512PrivateKey(sha512pk);
		char maskingKey [128];
		Utils::hex(sha512pk,32,maskingKey);

		httplib::Client htcli(_lfNodeHost.c_str(),_lfNodePort,600);
		int64_t timeRangeStart = 0;
		while (_running) {
			{
				std::lock_guard<std::mutex> sl(_state_l);
				for(auto ns=_state.begin();ns!=_state.end();++ns) {
					if (ns->second.dirty) {
						nlohmann::json network;
						if (get(ns->first,network)) {
							nlohmann::json newrec,selector0;
							selector0["Name"] = networksSelectorName;
							selector0["Ordinal"] = ns->first;
							newrec["Selectors"].push_back(selector0);
							newrec["Value"] = network.dump();
							newrec["OwnerPrivate"] = _lfOwnerPrivate;
							newrec["MaskingKey"] = maskingKey;
							newrec["PulseIfUnchanged"] = true;
							auto resp = htcli.Post("/makerecord",newrec.dump(),"application/json");
							if (resp) {
								if (resp->status == 200) {
									ns->second.dirty = false;
									printf("SET network %.16llx %s\n",ns->first,resp->body.c_str());
								} else {
									fprintf(stderr,"ERROR: LFDB: %d from node (create/update network): %s" ZT_EOL_S,resp->status,resp->body.c_str());
								}
							} else {
								fprintf(stderr,"ERROR: LFDB: node is offline" ZT_EOL_S);
							}
						}
					}

					for(auto ms=ns->second.members.begin();ms!=ns->second.members.end();++ms) {
						if ((_storeOnlineState)&&(ms->second.lastOnlineDirty)&&(ms->second.lastOnlineAddress)) {
							nlohmann::json newrec,selector0,selector1,selectors,ip;
							char tmp[1024],tmp2[128];
							OSUtils::ztsnprintf(tmp,sizeof(tmp),"com.zerotier.controller.lfdb:%s/network/%.16llx/online",controllerAddress,(unsigned long long)ns->first);
							ms->second.lastOnlineAddress.toIpString(tmp2);
							selector0["Name"] = tmp;
							selector0["Ordinal"] = ms->first;
							selector1["Name"] = tmp2;
							selector1["Ordinal"] = 0;
							selectors.push_back(selector0);
							selectors.push_back(selector1);
							newrec["Selectors"] = selectors;
							const uint8_t *const rawip = (const uint8_t *)ms->second.lastOnlineAddress.rawIpData();
							switch(ms->second.lastOnlineAddress.ss_family) {
								case AF_INET:
									for(int j=0;j<4;++j)
										ip.push_back((unsigned int)rawip[j]);
									break;
								case AF_INET6:
									for(int j=0;j<16;++j)
										ip.push_back((unsigned int)rawip[j]);
									break;
								default:
									ip = tmp2; // should never happen since only IP transport is currently supported
									break;
							}
							newrec["Value"] = ip;
							newrec["OwnerPrivate"] = _lfOwnerPrivate;
							newrec["MaskingKey"] = maskingKey;
							newrec["Timestamp"] = ms->second.lastOnlineTime;
							newrec["PulseIfUnchanged"] = true;
							auto resp = htcli.Post("/makerecord",newrec.dump(),"application/json");
							if (resp) {
								if (resp->status == 200) {
									ms->second.lastOnlineDirty = false;
									printf("SET member online %.16llx %.10llx %s\n",ns->first,ms->first,resp->body.c_str());
								} else {
									fprintf(stderr,"ERROR: LFDB: %d from node (create/update member online status): %s" ZT_EOL_S,resp->status,resp->body.c_str());
								}
							} else {
								fprintf(stderr,"ERROR: LFDB: node is offline" ZT_EOL_S);
							}
						}

						if (ms->second.dirty) {
							nlohmann::json network,member;
							if (get(ns->first,network,ms->first,member)) {
								nlohmann::json newrec,selector0,selector1,selectors;
								selector0["Name"] = networksSelectorName;
								selector0["Ordinal"] = ns->first;
								selector1["Name"] = membersSelectorName;
								selector1["Ordinal"] = ms->first;
								selectors.push_back(selector0);
								selectors.push_back(selector1);
								newrec["Selectors"] = selectors;
								newrec["Value"] = member.dump();
								newrec["OwnerPrivate"] = _lfOwnerPrivate;
								newrec["MaskingKey"] = maskingKey;
								newrec["PulseIfUnchanged"] = true;
								auto resp = htcli.Post("/makerecord",newrec.dump(),"application/json");
								if (resp) {
									if (resp->status == 200) {
										ms->second.dirty = false;
										printf("SET member %.16llx %.10llx %s\n",ns->first,ms->first,resp->body.c_str());
									} else {
										fprintf(stderr,"ERROR: LFDB: %d from node (create/update member): %s" ZT_EOL_S,resp->status,resp->body.c_str());
									}
								} else {
									fprintf(stderr,"ERROR: LFDB: node is offline" ZT_EOL_S);
								}
							}
						}
					}
				}
			}

			{
				std::ostringstream query;
				query
					<< '{'
						<< "\"Ranges\":[{"
							<< "\"Name\":\"" << networksSelectorName << "\","
							<< "\"Range\":[0,18446744073709551615]"
						<< "}],"
						<< "\"TimeRange\":[" << timeRangeStart << ",18446744073709551615],"
						<< "\"MaskingKey\":\"" << maskingKey << "\","
						<< "\"Owners\":[\"" << _lfOwnerPublic << "\"]"
					<< '}';
				auto resp = htcli.Post("/query",query.str(),"application/json");
				if (resp) {
					if (resp->status == 200) {
						nlohmann::json results(OSUtils::jsonParse(resp->body));
						if ((results.is_array())&&(results.size() > 0)) {
							for(std::size_t ri=0;ri<results.size();++ri) {
								nlohmann::json &rset = results[ri];
								if ((rset.is_array())&&(rset.size() > 0)) {
									nlohmann::json &result = rset[0];
									if (result.is_object()) {
										nlohmann::json &record = result["Record"];
										if (record.is_object()) {
											const std::string recordValue = result["Value"];
											printf("GET network %s\n",recordValue.c_str());
											nlohmann::json network(OSUtils::jsonParse(recordValue));
											if (network.is_object()) {
												const std::string idstr = network["id"];
												const uint64_t id = Utils::hexStrToU64(idstr.c_str());
												if ((id >> 24) == controllerAddressInt) { // sanity check

													std::lock_guard<std::mutex> sl(_state_l);
													_NetworkState &ns = _state[id];
													if (!ns.dirty) {
														nlohmann::json oldNetwork;
														if (get(id,oldNetwork)) {
															const uint64_t revision = network["revision"];
															const uint64_t prevRevision = oldNetwork["revision"];
															if (prevRevision < revision) {
																_networkChanged(oldNetwork,network,timeRangeStart > 0);
															}
														} else {
															nlohmann::json nullJson;
															_networkChanged(nullJson,network,timeRangeStart > 0);
														}
													}

												}
											}
										}
									}
								}
							}
						}
					} else {
						fprintf(stderr,"ERROR: LFDB: %d from node: %s" ZT_EOL_S,resp->status,resp->body.c_str());
					}
				} else {
					fprintf(stderr,"ERROR: LFDB: node is offline" ZT_EOL_S);
				}
			}

			{
				std::ostringstream query;
				query
					<< '{'
						<< "\"Ranges\":[{"
							<< "\"Name\":\"" << networksSelectorName << "\","
							<< "\"Range\":[0,18446744073709551615]"
						<< "},{"
							<< "\"Name\":\"" << membersSelectorName << "\","
							<< "\"Range\":[0,18446744073709551615]"
						<< "}],"
						<< "\"TimeRange\":[" << timeRangeStart << ",18446744073709551615],"
						<< "\"MaskingKey\":\"" << maskingKey << "\","
						<< "\"Owners\":[\"" << _lfOwnerPublic << "\"]"
					<< '}';
				auto resp = htcli.Post("/query",query.str(),"application/json");
				if (resp) {
					if (resp->status == 200) {
						nlohmann::json results(OSUtils::jsonParse(resp->body));
						if ((results.is_array())&&(results.size() > 0)) {
							for(std::size_t ri=0;ri<results.size();++ri) {
								nlohmann::json &rset = results[ri];
								if ((rset.is_array())&&(rset.size() > 0)) {
									nlohmann::json &result = rset[0];
									if (result.is_object()) {
										nlohmann::json &record = result["Record"];
										if (record.is_object()) {
											const std::string recordValue = result["Value"];
											printf("GET member %s\n",recordValue.c_str());
											nlohmann::json member(OSUtils::jsonParse(recordValue));
											if (member.is_object()) {
												const std::string nwidstr = member["nwid"];
												const std::string idstr = member["id"];
												const uint64_t nwid = Utils::hexStrToU64(nwidstr.c_str());
												const uint64_t id = Utils::hexStrToU64(idstr.c_str());
												if ((id)&&((nwid >> 24) == controllerAddressInt)) { // sanity check

													std::lock_guard<std::mutex> sl(_state_l);
													auto ns = _state.find(nwid);
													if ((ns == _state.end())||(!ns->second.members[id].dirty)) {
														nlohmann::json network,oldMember;
														if (get(nwid,network,id,oldMember)) {
															const uint64_t revision = member["revision"];
															const uint64_t prevRevision = oldMember["revision"];
															if (prevRevision < revision)
																_memberChanged(oldMember,member,timeRangeStart > 0);
														}
													} else {
														nlohmann::json nullJson;
														_memberChanged(nullJson,member,timeRangeStart > 0);
													}

												}
											}
										}
									}
								}
							}
						}
					} else {
						fprintf(stderr,"ERROR: LFDB: %d from node: %s" ZT_EOL_S,resp->status,resp->body.c_str());
					}
				} else {
					fprintf(stderr,"ERROR: LFDB: node is offline" ZT_EOL_S);
				}
			}

			timeRangeStart = time(nullptr) - 120; // start next query 2m before now to avoid losing updates
			_ready = true;

			for(int k=0;k<20;++k) { // 2s delay between queries for remotely modified networks or members
				if (!_running)
					return;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
	});
}

LFDB::~LFDB()
{
	_running = false;
	_syncThread.join();
}

bool LFDB::waitForReady()
{
	while (!_ready) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return true;
}

bool LFDB::isReady()
{
	return (_ready);
}

void LFDB::save(nlohmann::json *orig,nlohmann::json &record)
{
	if (orig) {
		if (*orig != record) {
			record["revision"] = OSUtils::jsonInt(record["revision"],0ULL) + 1;
		}
	} else {
		record["revision"] = 1;
	}

	const std::string objtype = record["objtype"];
	if (objtype == "network") {
		const uint64_t nwid = OSUtils::jsonIntHex(record["id"],0ULL);
		if (nwid) {
			nlohmann::json old;
			get(nwid,old);
			if ((!old.is_object())||(old != record)) {
				_networkChanged(old,record,true);
				{
					std::lock_guard<std::mutex> l(_state_l);
					_state[nwid].dirty = true;
				}
			}
		}
	} else if (objtype == "member") {
		const uint64_t nwid = OSUtils::jsonIntHex(record["nwid"],0ULL);
		const uint64_t id = OSUtils::jsonIntHex(record["id"],0ULL);
		if ((id)&&(nwid)) {
			nlohmann::json network,old;
			get(nwid,network,id,old);
			if ((!old.is_object())||(old != record)) {
				_memberChanged(old,record,true);
				{
					std::lock_guard<std::mutex> l(_state_l);
					_state[nwid].members[id].dirty = true;
				}
			}
		}
	}
}

void LFDB::eraseNetwork(const uint64_t networkId)
{
	// TODO
}

void LFDB::eraseMember(const uint64_t networkId,const uint64_t memberId)
{
	// TODO
}

void LFDB::nodeIsOnline(const uint64_t networkId,const uint64_t memberId,const InetAddress &physicalAddress)
{
	std::lock_guard<std::mutex> l(_state_l);
	auto nw = _state.find(networkId);
	if (nw != _state.end()) {
		auto m = nw->second.members.find(memberId);
		if (m != nw->second.members.end()) {
			m->second.lastOnlineTime = OSUtils::now();
			if (physicalAddress)
				m->second.lastOnlineAddress = physicalAddress;
			m->second.lastOnlineDirty = true;
		}
	}
}

} // namespace ZeroTier
