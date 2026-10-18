/*=============================================================================
	WebRTCLan.h — the IpDrv-facing face of the synthetic LAN.

	Deliberately free of IpDrv types (no AUdpLink, no sockaddr): IpDrv calls into
	WebRTCDrv, never the reverse, so there is no module cycle. The link is
	identified by an opaque id — AUdpLink passes its own `this`.

	See WebRTCLan.cpp for what this simulates and why it has to be at the UDP layer.
=============================================================================*/

#ifndef WEBRTCLAN_H
#define WEBRTCLAN_H

/** A UdpLink bound a port. Binding the beacon port starts the passive (ClientBeaconReceiver) discovery. */
void WebRTCLan_Bind( const void* LinkId, INT Port );

/** A UdpLink went away — drop its pending replies. */
void WebRTCLan_Unbind( const void* LinkId );

/**
 * A UdpLink is sending. Returns TRUE if the synthetic LAN handled it, in which case the caller must
 * NOT attempt a real sendto (there is no UDP in a browser, and a failed send only logs a red herring).
 */
UBOOL WebRTCLan_Send( const void* LinkId, DWORD DestAddr, INT DestPort, const TCHAR* Text );

/**
 * Pop one synthetic packet for this link, to be delivered through the stock eventReceivedText path.
 * Also drives the discovery state machine, so it must be called every tick of a bound link.
 */
UBOOL WebRTCLan_NextReply( const void* LinkId, DWORD* OutAddr, INT* OutPort, FString& OutText );

/** The synthetic dotted address that stands for `Room` (stable for the session). */
FString WebRTCLan_HostForRoom( const TCHAR* Room );

/** Reverse: the room a synthetic address means, or "" if the host is not one of ours. */
FString WebRTCLan_RoomForHost( const TCHAR* Host );

/** Told by the net driver when we are hosting, so our own room is not advertised back to us. */
void WebRTCLan_SetLocalRoom( const TCHAR* Room );

#endif // WEBRTCLAN_H
