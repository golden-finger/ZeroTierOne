CORE_OBJS=\
	node/C25519.o \
	node/Capability.o \
	node/CertificateOfMembership.o \
	node/CertificateOfOwnership.o \
	node/Identity.o \
	node/IncomingPacket.o \
	node/InetAddress.o \
	node/Membership.o \
	node/Multicaster.o \
	node/Network.o \
	node/NetworkConfig.o \
	node/Node.o \
	node/OutboundMulticast.o \
	node/Packet.o \
	node/Path.o \
	node/Peer.o \
	node/Poly1305.o \
	node/Revocation.o \
	node/Salsa20.o \
	node/SelfAwareness.o \
	node/SHA512.o \
	node/Switch.o \
	node/Tag.o \
	node/Topology.o \
	node/Trace.o \
	node/Utils.o

ONE_OBJS=\
	controller/EmbeddedNetworkController.o \
	controller/DB.o \
	controller/FileDB.o \
	controller/LFDB.o \
	controller/PostgreSQL.o \
	controller/RabbitMQ.o \
	osdep/ManagedRoute.o \
	osdep/Http.o \
	osdep/OSUtils.o \
	service/SoftwareUpdater.o \
	service/OneService.o

