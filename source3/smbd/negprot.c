/* 
   Unix SMB/CIFS implementation.
   negprot reply code
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) Volker Lendecke 2007

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "smbd/smbd.h"
#include "smbd/globals.h"
#include "../libcli/auth/spnego.h"
#include "serverid.h"
#include "auth.h"
#include "messages.h"
#include "smbprofile.h"
#include "auth/gensec/gensec.h"
#include "../libcli/smb/smb_signing.h"

extern fstring remote_proto;

static void get_challenge(struct smbd_server_connection *sconn, uint8 buff[8])
{
	NTSTATUS nt_status;

	/* We might be called more than once, multiple negprots are
	 * permitted */
	if (sconn->smb1.negprot.auth_context) {
		DEBUG(3, ("get challenge: is this a secondary negprot? "
			  "sconn->negprot.auth_context is non-NULL!\n"));
			TALLOC_FREE(sconn->smb1.negprot.auth_context);
	}

	DEBUG(10, ("get challenge: creating negprot_global_auth_context\n"));
	nt_status = make_auth4_context(
		sconn, &sconn->smb1.negprot.auth_context);
	if (!NT_STATUS_IS_OK(nt_status)) {
		DEBUG(0, ("make_auth_context_subsystem returned %s",
			  nt_errstr(nt_status)));
		smb_panic("cannot make_negprot_global_auth_context!");
	}
	DEBUG(10, ("get challenge: getting challenge\n"));
	sconn->smb1.negprot.auth_context->get_ntlm_challenge(
		sconn->smb1.negprot.auth_context, buff);
}

/****************************************************************************
 Reply for the lanman 1.0 protocol.
****************************************************************************/

static void reply_lanman1(struct smb_request *req, uint16 choice)
{
	int secword=0;
	time_t t = time(NULL);
	struct smbd_server_connection *sconn = req->sconn;
	uint16_t raw;
	if (lp_async_smb_echo_handler()) {
		raw = 0;
	} else {
		raw = (lp_read_raw()?1:0) | (lp_write_raw()?2:0);
	}

	sconn->smb1.negprot.encrypted_passwords = lp_encrypt_passwords();

	secword |= NEGOTIATE_SECURITY_USER_LEVEL;
	if (sconn->smb1.negprot.encrypted_passwords) {
		secword |= NEGOTIATE_SECURITY_CHALLENGE_RESPONSE;
	}

	reply_outbuf(req, 13, sconn->smb1.negprot.encrypted_passwords?8:0);

	SSVAL(req->outbuf,smb_vwv0,choice);
	SSVAL(req->outbuf,smb_vwv1,secword);
	/* Create a token value and add it to the outgoing packet. */
	if (sconn->smb1.negprot.encrypted_passwords) {
		get_challenge(sconn, (uint8 *)smb_buf(req->outbuf));
		SSVAL(req->outbuf,smb_vwv11, 8);
	}

	smbXsrv_connection_init_tables(req->sconn->conn, PROTOCOL_LANMAN1);

	/* Reply, SMBlockread, SMBwritelock supported. */
	SCVAL(req->outbuf,smb_flg, FLAG_REPLY|FLAG_SUPPORT_LOCKREAD);
	SSVAL(req->outbuf,smb_vwv2, sconn->smb1.negprot.max_recv);
	SSVAL(req->outbuf,smb_vwv3, lp_max_mux()); /* maxmux */
	SSVAL(req->outbuf,smb_vwv4, 1);
	SSVAL(req->outbuf,smb_vwv5, raw); /* tell redirector we support
		readbraw writebraw (possibly) */
	SIVAL(req->outbuf,smb_vwv6, getpid());
	SSVAL(req->outbuf,smb_vwv10, set_server_zone_offset(t)/60);

	srv_put_dos_date((char *)req->outbuf,smb_vwv8,t);

	return;
}

/****************************************************************************
 Reply for the lanman 2.0 protocol.
****************************************************************************/

static void reply_lanman2(struct smb_request *req, uint16 choice)
{
	int secword=0;
	time_t t = time(NULL);
	struct smbd_server_connection *sconn = req->sconn;
	uint16_t raw;
	if (lp_async_smb_echo_handler()) {
		raw = 0;
	} else {
		raw = (lp_read_raw()?1:0) | (lp_write_raw()?2:0);
	}

	sconn->smb1.negprot.encrypted_passwords = lp_encrypt_passwords();

	secword |= NEGOTIATE_SECURITY_USER_LEVEL;
	if (sconn->smb1.negprot.encrypted_passwords) {
		secword |= NEGOTIATE_SECURITY_CHALLENGE_RESPONSE;
	}

	reply_outbuf(req, 13, sconn->smb1.negprot.encrypted_passwords?8:0);

	SSVAL(req->outbuf,smb_vwv0, choice);
	SSVAL(req->outbuf,smb_vwv1, secword);
	SIVAL(req->outbuf,smb_vwv6, getpid());

	/* Create a token value and add it to the outgoing packet. */
	if (sconn->smb1.negprot.encrypted_passwords) {
		get_challenge(sconn, (uint8 *)smb_buf(req->outbuf));
		SSVAL(req->outbuf,smb_vwv11, 8);
	}

	smbXsrv_connection_init_tables(req->sconn->conn, PROTOCOL_LANMAN2);

	/* Reply, SMBlockread, SMBwritelock supported. */
	SCVAL(req->outbuf,smb_flg,FLAG_REPLY|FLAG_SUPPORT_LOCKREAD);
	SSVAL(req->outbuf,smb_vwv2,sconn->smb1.negprot.max_recv);
	SSVAL(req->outbuf,smb_vwv3,lp_max_mux());
	SSVAL(req->outbuf,smb_vwv4,1);
	SSVAL(req->outbuf,smb_vwv5,raw); /* readbraw and/or writebraw */
	SSVAL(req->outbuf,smb_vwv10, set_server_zone_offset(t)/60);
	srv_put_dos_date((char *)req->outbuf,smb_vwv8,t);
}

/****************************************************************************
 Generate the spnego negprot reply blob. Return the number of bytes used.
****************************************************************************/

DATA_BLOB negprot_spnego(TALLOC_CTX *ctx, struct smbd_server_connection *sconn)
{
	DATA_BLOB blob = data_blob_null;
	DATA_BLOB blob_out = data_blob_null;
	nstring dos_name;
	fstring unix_name;
	NTSTATUS status;
#ifdef DEVELOPER
	size_t slen;
#endif
	struct gensec_security *gensec_security;

	/* See if we can get an SPNEGO blob */
	status = auth_generic_prepare(talloc_tos(),
				      sconn->remote_address,
				      &gensec_security);
	if (NT_STATUS_IS_OK(status)) {
		status = gensec_start_mech_by_oid(gensec_security, GENSEC_OID_SPNEGO);
		if (NT_STATUS_IS_OK(status)) {
			status = gensec_update(gensec_security, ctx,
					       data_blob_null, &blob);
			/* If we get the list of OIDs, the 'OK' answer
			 * is NT_STATUS_MORE_PROCESSING_REQUIRED */
			if (!NT_STATUS_EQUAL(status, NT_STATUS_MORE_PROCESSING_REQUIRED)) {
				DEBUG(0, ("Failed to start SPNEGO handler for negprot OID list!\n"));
				blob = data_blob_null;
			}
		}
		TALLOC_FREE(gensec_security);
	}

	sconn->smb1.negprot.spnego = true;

	/* strangely enough, NT does not sent the single OID NTLMSSP when
	   not a ADS member, it sends no OIDs at all

	   OLD COMMENT : "we can't do this until we teach our sesssion setup parser to know
		   about raw NTLMSSP (clients send no ASN.1 wrapping if we do this)"

	   Our sessionsetup code now handles raw NTLMSSP connects, so we can go
	   back to doing what W2K3 does here. This is needed to make PocketPC 2003
	   CIFS connections work with SPNEGO. See bugzilla bugs #1828 and #3133
	   for details. JRA.

	*/

	if (blob.length == 0 || blob.data == NULL) {
		return data_blob_null;
	}

	blob_out = data_blob_talloc(ctx, NULL, 16 + blob.length);
	if (blob_out.data == NULL) {
		data_blob_free(&blob);
		return data_blob_null;
	}

	memset(blob_out.data, '\0', 16);

	checked_strlcpy(unix_name, lp_netbios_name(), sizeof(unix_name));
	(void)strlower_m(unix_name);
	push_ascii_nstring(dos_name, unix_name);
	strlcpy((char *)blob_out.data, dos_name, 17);

#ifdef DEVELOPER
	/* Fix valgrind 'uninitialized bytes' issue. */
	slen = strlen(dos_name);
	if (slen < 16) {
		memset(blob_out.data+slen, '\0', 16 - slen);
	}
#endif

	memcpy(&blob_out.data[16], blob.data, blob.length);

	data_blob_free(&blob);

	return blob_out;
}

/****************************************************************************
 Reply for the nt protocol.
****************************************************************************/

static void reply_nt1(struct smb_request *req, uint16 choice)
{
	/* dual names + lock_and_read + nt SMBs + remote API calls */
	int capabilities = CAP_NT_FIND|CAP_LOCK_AND_READ|
		CAP_LEVEL_II_OPLOCKS;

	int secword=0;
	bool negotiate_spnego = False;
	struct timespec ts;
	ssize_t ret;
	struct smbd_server_connection *sconn = req->sconn;
	bool signing_desired = false;
	bool signing_required = false;

	sconn->smb1.negprot.encrypted_passwords = lp_encrypt_passwords();

	/* Check the flags field to see if this is Vista.
	   WinXP sets it and Vista does not. But we have to 
	   distinguish from NT which doesn't set it either. */

	if ( (req->flags2 & FLAGS2_EXTENDED_SECURITY) &&
		((req->flags2 & FLAGS2_SMB_SECURITY_SIGNATURES_REQUIRED) == 0) )
	{
		if (get_remote_arch() != RA_SAMBA) {
			set_remote_arch( RA_VISTA );
		}
	}

	reply_outbuf(req,17,0);

	/* do spnego in user level security if the client
	   supports it and we can do encrypted passwords */

	if (sconn->smb1.negprot.encrypted_passwords &&
	    lp_use_spnego() &&
	    (req->flags2 & FLAGS2_EXTENDED_SECURITY)) {
		negotiate_spnego = True;
		capabilities |= CAP_EXTENDED_SECURITY;
		add_to_common_flags2(FLAGS2_EXTENDED_SECURITY);
		/* Ensure FLAGS2_EXTENDED_SECURITY gets set in this reply
		   (already partially constructed. */
		SSVAL(req->outbuf, smb_flg2,
		      req->flags2 | FLAGS2_EXTENDED_SECURITY);
	}

	capabilities |= CAP_NT_SMBS|CAP_RPC_REMOTE_APIS;

	if (lp_unicode()) {
		capabilities |= CAP_UNICODE;
	}

	if (lp_unix_extensions()) {
		capabilities |= CAP_UNIX;
	}

	if (lp_large_readwrite())
		capabilities |= CAP_LARGE_READX|CAP_LARGE_WRITEX|CAP_W2K_SMBS;

	capabilities |= CAP_LARGE_FILES;

	if (!lp_async_smb_echo_handler() && lp_read_raw() && lp_write_raw())
		capabilities |= CAP_RAW_MODE;

	if (lp_nt_status_support())
		capabilities |= CAP_STATUS32;

	if (lp_host_msdfs())
		capabilities |= CAP_DFS;

	secword |= NEGOTIATE_SECURITY_USER_LEVEL;
	if (sconn->smb1.negprot.encrypted_passwords) {
		secword |= NEGOTIATE_SECURITY_CHALLENGE_RESPONSE;
	}

	signing_desired = smb_signing_is_desired(req->sconn->smb1.signing_state);
	signing_required = smb_signing_is_mandatory(req->sconn->smb1.signing_state);

	if (signing_desired) {
		secword |= NEGOTIATE_SECURITY_SIGNATURES_ENABLED;
		/* No raw mode with smb signing. */
		capabilities &= ~CAP_RAW_MODE;
		if (signing_required) {
			secword |=NEGOTIATE_SECURITY_SIGNATURES_REQUIRED;
		}
	}

	SSVAL(req->outbuf,smb_vwv0,choice);
	SCVAL(req->outbuf,smb_vwv1,secword);

	smbXsrv_connection_init_tables(req->sconn->conn, PROTOCOL_NT1);

	SSVAL(req->outbuf,smb_vwv1+1, lp_max_mux()); /* maxmpx */
	SSVAL(req->outbuf,smb_vwv2+1, 1); /* num vcs */
	SIVAL(req->outbuf,smb_vwv3+1,
	      sconn->smb1.negprot.max_recv); /* max buffer. LOTS! */
	SIVAL(req->outbuf,smb_vwv5+1, 0x10000); /* raw size. full 64k */
	SIVAL(req->outbuf,smb_vwv7+1, getpid()); /* session key */
	SIVAL(req->outbuf,smb_vwv9+1, capabilities); /* capabilities */
	clock_gettime(CLOCK_REALTIME,&ts);
	put_long_date_timespec(TIMESTAMP_SET_NT_OR_BETTER,(char *)req->outbuf+smb_vwv11+1,ts);
	SSVALS(req->outbuf,smb_vwv15+1,set_server_zone_offset(ts.tv_sec)/60);

	if (!negotiate_spnego) {
		/* Create a token value and add it to the outgoing packet. */
		if (sconn->smb1.negprot.encrypted_passwords) {
			uint8 chal[8];
			/* note that we do not send a challenge at all if
			   we are using plaintext */
			get_challenge(sconn, chal);
			ret = message_push_blob(
				&req->outbuf, data_blob_const(chal, sizeof(chal)));
			if (ret == -1) {
				DEBUG(0, ("Could not push challenge\n"));
				reply_nterror(req, NT_STATUS_NO_MEMORY);
				return;
			}
			SCVAL(req->outbuf, smb_vwv16+1, ret);
		}
		ret = message_push_string(&req->outbuf, lp_workgroup(),
					  STR_UNICODE|STR_TERMINATE
					  |STR_NOALIGN);
		if (ret == -1) {
			DEBUG(0, ("Could not push workgroup string\n"));
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}
		ret = message_push_string(&req->outbuf, lp_netbios_name(),
					  STR_UNICODE|STR_TERMINATE
					  |STR_NOALIGN);
		if (ret == -1) {
			DEBUG(0, ("Could not push netbios name string\n"));
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}
		DEBUG(3,("not using SPNEGO\n"));
	} else {
		DATA_BLOB spnego_blob = negprot_spnego(req, req->sconn);

		if (spnego_blob.data == NULL) {
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}

		ret = message_push_blob(&req->outbuf, spnego_blob);
		if (ret == -1) {
			DEBUG(0, ("Could not push spnego blob\n"));
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}
		data_blob_free(&spnego_blob);

		SCVAL(req->outbuf,smb_vwv16+1, 0);
		DEBUG(3,("using SPNEGO\n"));
	}

	return;
}

/* these are the protocol lists used for auto architecture detection:

WinNT 3.51:
protocol [PC NETWORK PROGRAM 1.0]
protocol [XENIX CORE]
protocol [MICROSOFT NETWORKS 1.03]
protocol [LANMAN1.0]
protocol [Windows for Workgroups 3.1a]
protocol [LM1.2X002]
protocol [LANMAN2.1]
protocol [NT LM 0.12]

Win95:
protocol [PC NETWORK PROGRAM 1.0]
protocol [XENIX CORE]
protocol [MICROSOFT NETWORKS 1.03]
protocol [LANMAN1.0]
protocol [Windows for Workgroups 3.1a]
protocol [LM1.2X002]
protocol [LANMAN2.1]
protocol [NT LM 0.12]

Win2K:
protocol [PC NETWORK PROGRAM 1.0]
protocol [LANMAN1.0]
protocol [Windows for Workgroups 3.1a]
protocol [LM1.2X002]
protocol [LANMAN2.1]
protocol [NT LM 0.12]

Vista:
protocol [PC NETWORK PROGRAM 1.0]
protocol [LANMAN1.0]
protocol [Windows for Workgroups 3.1a]
protocol [LM1.2X002]
protocol [LANMAN2.1]
protocol [NT LM 0.12]
protocol [SMB 2.001]

OS/2:
protocol [PC NETWORK PROGRAM 1.0]
protocol [XENIX CORE]
protocol [LANMAN1.0]
protocol [LM1.2X002]
protocol [LANMAN2.1]
*/

/*
  * Modified to recognize the architecture of the remote machine better.
  *
  * This appears to be the matrix of which protocol is used by which
  * MS product.
       Protocol                       WfWg    Win95   WinNT  Win2K  OS/2 Vista
       PC NETWORK PROGRAM 1.0          1       1       1      1      1     1
       XENIX CORE                                      2             2
       MICROSOFT NETWORKS 3.0          2       2       
       DOS LM1.2X002                   3       3       
       MICROSOFT NETWORKS 1.03                         3
       DOS LANMAN2.1                   4       4       
       LANMAN1.0                                       4      2      3     2
       Windows for Workgroups 3.1a     5       5       5      3            3
       LM1.2X002                                       6      4      4     4
       LANMAN2.1                                       7      5      5     5
       NT LM 0.12                              6       8      6            6
       SMB 2.001                                                           7
  *
  *  tim@fsg.com 09/29/95
  *  Win2K added by matty 17/7/99
  */

#define ARCH_WFWG     0x3      /* This is a fudge because WfWg is like Win95 */
#define ARCH_WIN95    0x2
#define ARCH_WINNT    0x4
#define ARCH_WIN2K    0xC      /* Win2K is like NT */
#define ARCH_OS2      0x14     /* Again OS/2 is like NT */
#define ARCH_SAMBA    0x20
#define ARCH_CIFSFS   0x40
#define ARCH_VISTA    0x8C     /* Vista is like XP/2K */

#define ARCH_ALL      0x7F

/* List of supported protocols, most desired first */
static const struct {
	const char *proto_name;
	const char *short_name;
	void (*proto_reply_fn)(struct smb_request *req, uint16 choice);
	int protocol_level;
} supported_protocols[] = {
	{"SMB 2.???",               "SMB2_FF",  reply_smb20ff,  PROTOCOL_SMB2_10},
	{"SMB 2.002",               "SMB2_02",  reply_smb2002,  PROTOCOL_SMB2_02},
	{"NT LANMAN 1.0",           "NT1",      reply_nt1,      PROTOCOL_NT1},
	{"NT LM 0.12",              "NT1",      reply_nt1,      PROTOCOL_NT1},
	{"POSIX 2",                 "NT1",      reply_nt1,      PROTOCOL_NT1},
	{"LANMAN2.1",               "LANMAN2",  reply_lanman2,  PROTOCOL_LANMAN2},
	{"LM1.2X002",               "LANMAN2",  reply_lanman2,  PROTOCOL_LANMAN2},
	{"Samba",                   "LANMAN2",  reply_lanman2,  PROTOCOL_LANMAN2},
	{"DOS LM1.2X002",           "LANMAN2",  reply_lanman2,  PROTOCOL_LANMAN2},
	{"LANMAN1.0",               "LANMAN1",  reply_lanman1,  PROTOCOL_LANMAN1},
	{"MICROSOFT NETWORKS 3.0",  "LANMAN1",  reply_lanman1,  PROTOCOL_LANMAN1},
	{NULL,NULL,NULL,0},
};

/****************************************************************************
 Reply to a negprot.
 conn POINTER CAN BE NULL HERE !
****************************************************************************/

void reply_negprot(struct smb_request *req)
{
	int choice= -1;
	int chosen_level = -1;
	int protocol;
	const char *p;
	int arch = ARCH_ALL;
	int num_cliprotos;
	char **cliprotos;
	int i;
	size_t converted_size;
	struct smbd_server_connection *sconn = req->sconn;

	START_PROFILE(SMBnegprot);

	if (sconn->smb1.negprot.done) {
		END_PROFILE(SMBnegprot);
		exit_server_cleanly("multiple negprot's are not permitted");
	}
	sconn->smb1.negprot.done = true;

	if (req->buflen == 0) {
		DEBUG(0, ("negprot got no protocols\n"));
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		END_PROFILE(SMBnegprot);
		return;
	}

	if (req->buf[req->buflen-1] != '\0') {
		DEBUG(0, ("negprot protocols not 0-terminated\n"));
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		END_PROFILE(SMBnegprot);
		return;
	}

	p = (const char *)req->buf + 1;

	num_cliprotos = 0;
	cliprotos = NULL;

	while (smbreq_bufrem(req, p) > 0) {

		char **tmp;

		tmp = talloc_realloc(talloc_tos(), cliprotos, char *,
					   num_cliprotos+1);
		if (tmp == NULL) {
			DEBUG(0, ("talloc failed\n"));
			TALLOC_FREE(cliprotos);
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			END_PROFILE(SMBnegprot);
			return;
		}

		cliprotos = tmp;

		if (!pull_ascii_talloc(cliprotos, &cliprotos[num_cliprotos], p,
				       &converted_size)) {
			DEBUG(0, ("pull_ascii_talloc failed\n"));
			TALLOC_FREE(cliprotos);
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			END_PROFILE(SMBnegprot);
			return;
		}

		DEBUG(3, ("Requested protocol [%s]\n",
			  cliprotos[num_cliprotos]));

		num_cliprotos += 1;
		p += strlen(p) + 2;
	}

	for (i=0; i<num_cliprotos; i++) {
		if (strcsequal(cliprotos[i], "Windows for Workgroups 3.1a"))
			arch &= ( ARCH_WFWG | ARCH_WIN95 | ARCH_WINNT
				  | ARCH_WIN2K );
		else if (strcsequal(cliprotos[i], "DOS LM1.2X002"))
			arch &= ( ARCH_WFWG | ARCH_WIN95 );
		else if (strcsequal(cliprotos[i], "DOS LANMAN2.1"))
			arch &= ( ARCH_WFWG | ARCH_WIN95 );
		else if (strcsequal(cliprotos[i], "NT LM 0.12"))
			arch &= ( ARCH_WIN95 | ARCH_WINNT | ARCH_WIN2K
				  | ARCH_CIFSFS);
		else if (strcsequal(cliprotos[i], "SMB 2.001"))
			arch = ARCH_VISTA;		
		else if (strcsequal(cliprotos[i], "LANMAN2.1"))
			arch &= ( ARCH_WINNT | ARCH_WIN2K | ARCH_OS2 );
		else if (strcsequal(cliprotos[i], "LM1.2X002"))
			arch &= ( ARCH_WINNT | ARCH_WIN2K | ARCH_OS2 );
		else if (strcsequal(cliprotos[i], "MICROSOFT NETWORKS 1.03"))
			arch &= ARCH_WINNT;
		else if (strcsequal(cliprotos[i], "XENIX CORE"))
			arch &= ( ARCH_WINNT | ARCH_OS2 );
		else if (strcsequal(cliprotos[i], "Samba")) {
			arch = ARCH_SAMBA;
			break;
		} else if (strcsequal(cliprotos[i], "POSIX 2")) {
			arch = ARCH_CIFSFS;
			break;
		}
	}

	/* CIFSFS can send one arch only, NT LM 0.12. */
	if (i == 1 && (arch & ARCH_CIFSFS)) {
		arch = ARCH_CIFSFS;
	}

	switch ( arch ) {
		case ARCH_CIFSFS:
			set_remote_arch(RA_CIFSFS);
			break;
		case ARCH_SAMBA:
			set_remote_arch(RA_SAMBA);
			break;
		case ARCH_WFWG:
			set_remote_arch(RA_WFWG);
			break;
		case ARCH_WIN95:
			set_remote_arch(RA_WIN95);
			break;
		case ARCH_WINNT:
			if(req->flags2 == FLAGS2_WIN2K_SIGNATURE)
				set_remote_arch(RA_WIN2K);
			else
				set_remote_arch(RA_WINNT);
			break;
		case ARCH_WIN2K:
			/* Vista may have been set in the negprot so don't 
			   override it here */
			if ( get_remote_arch() != RA_VISTA )
				set_remote_arch(RA_WIN2K);
			break;
		case ARCH_VISTA:
			set_remote_arch(RA_VISTA);
			break;
		case ARCH_OS2:
			set_remote_arch(RA_OS2);
			break;
		default:
			set_remote_arch(RA_UNKNOWN);
		break;
	}

	/* possibly reload - change of architecture */
	reload_services(sconn, conn_snum_used, true);

	/* moved from the netbios session setup code since we don't have that 
	   when the client connects to port 445.  Of course there is a small
	   window where we are listening to messages   -- jerry */

	serverid_register(messaging_server_id(sconn->msg_ctx),
			  FLAG_MSG_GENERAL|FLAG_MSG_SMBD
			  |FLAG_MSG_PRINT_GENERAL);

	/* Check for protocols, most desirable first */
	for (protocol = 0; supported_protocols[protocol].proto_name; protocol++) {
		i = 0;
		if ((supported_protocols[protocol].protocol_level <= lp_server_max_protocol()) &&
				(supported_protocols[protocol].protocol_level >= lp_server_min_protocol()))
			while (i < num_cliprotos) {
				if (strequal(cliprotos[i],supported_protocols[protocol].proto_name)) {
					choice = i;
					chosen_level = supported_protocols[protocol].protocol_level;
				}
				i++;
			}
		if(choice != -1)
			break;
	}

	if(choice != -1) {
		fstrcpy(remote_proto,supported_protocols[protocol].short_name);
		reload_services(sconn, conn_snum_used, true);
		supported_protocols[protocol].proto_reply_fn(req, choice);
		DEBUG(3,("Selected protocol %s\n",supported_protocols[protocol].proto_name));
	} else {
		DEBUG(0,("No protocol supported !\n"));
		reply_outbuf(req, 1, 0);
		SSVAL(req->outbuf, smb_vwv0, choice);
	}

	DEBUG( 5, ( "negprot index=%d\n", choice ) );

	if ((lp_server_signing() == SMB_SIGNING_REQUIRED)
	    && (chosen_level < PROTOCOL_NT1)) {
		exit_server_cleanly("SMB signing is required and "
			"client negotiated a downlevel protocol");
	}

	TALLOC_FREE(cliprotos);

	if (lp_async_smb_echo_handler() && (chosen_level < PROTOCOL_SMB2_02) &&
	    !fork_echo_handler(sconn)) {
		exit_server("Failed to fork echo handler");
	}

	END_PROFILE(SMBnegprot);
	return;
}