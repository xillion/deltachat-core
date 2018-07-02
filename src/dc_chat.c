/*******************************************************************************
 *
 *                              Delta Chat Core
 *                      Copyright (C) 2017 Björn Petersen
 *                   Contact: r10s@b44t.com, http://b44t.com
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see http://www.gnu.org/licenses/ .
 *
 ******************************************************************************/


#include "dc_context.h"
#include "dc_job.h"
#include "dc_smtp.h"
#include "dc_imap.h"
#include "dc_mimefactory.h"


#define DC_CHAT_MAGIC 0xc4a7c4a7


/**
 * Create a chat object in memory.
 *
 * @private @memberof dc_chat_t
 *
 * @param context The context that should be stored in the chat object.
 *
 * @return New and empty chat object, must be freed using dc_chat_unref().
 */
dc_chat_t* dc_chat_new(dc_context_t* context)
{
	dc_chat_t* chat = NULL;

	if( context == NULL || (chat=calloc(1, sizeof(dc_chat_t)))==NULL ) {
		exit(14); /* cannot allocate little memory, unrecoverable error */
	}

	chat->m_magic    = DC_CHAT_MAGIC;
	chat->m_context  = context;
	chat->m_type     = DC_CHAT_TYPE_UNDEFINED;
	chat->m_param    = dc_param_new();

    return chat;
}


/**
 * Free a chat object.
 *
 * @memberof dc_chat_t
 *
 * @param chat Chat object are returned eg. by dc_get_chat().
 *
 * @return None.
 */
void dc_chat_unref(dc_chat_t* chat)
{
	if( chat==NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return;
	}

	dc_chat_empty(chat);
	dc_param_unref(chat->m_param);
	chat->m_magic = 0;
	free(chat);
}


/**
 * Empty a chat object.
 *
 * @private @memberof dc_chat_t
 *
 * @param chat The chat object to empty.
 *
 * @return None.
 */
void dc_chat_empty(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return;
	}

	free(chat->m_name);
	chat->m_name = NULL;

	chat->m_draft_timestamp = 0;

	free(chat->m_draft_text);
	chat->m_draft_text = NULL;

	chat->m_type = DC_CHAT_TYPE_UNDEFINED;
	chat->m_id   = 0;

	free(chat->m_grpid);
	chat->m_grpid = NULL;

	chat->m_blocked = 0;

	dc_param_set_packed(chat->m_param, NULL);
}


/*******************************************************************************
 * Getters
 ******************************************************************************/


/**
 * Get chat ID. The chat ID is the ID under which the chat is filed in the database.
 *
 * Special IDs:
 * - DC_CHAT_ID_DEADDROP         (1) - Virtual chat containing messages which senders are not confirmed by the user.
 * - DC_CHAT_ID_STARRED          (5) - Virtual chat containing all starred messages-
 * - DC_CHAT_ID_ARCHIVED_LINK    (6) - A link at the end of the chatlist, if present the UI should show the button "Archived chats"-
 *
 * "Normal" chat IDs are larger than these special IDs (larger than DC_CHAT_ID_LAST_SPECIAL).
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return Chat ID. 0 on errors.
 */
uint32_t dc_chat_get_id(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return 0;
	}

	return chat->m_id;
}


/**
 * Get chat type.
 *
 * Currently, there are two chat types:
 *
 * - DC_CHAT_TYPE_SINGLE (100) - a normal chat is a chat with a single contact,
 *   chats_contacts contains one record for the user.  DC_CONTACT_ID_SELF
 *   (see dc_contact_t::m_id) is added _only_ for a self talk.
 *
 * - DC_CHAT_TYPE_GROUP  (120) - a group chat, chats_contacts conain all group
 *   members, incl. DC_CONTACT_ID_SELF
 *
 * - DC_CHAT_TYPE_VERIFIED_GROUP  (130) - a verified group chat. In verified groups,
 *   all members are verified and encryption is always active and cannot be disabled.
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return Chat type.
 */
int dc_chat_get_type(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return DC_CHAT_TYPE_UNDEFINED;
	}
	return chat->m_type;
}


/**
 * Get name of a chat. For one-to-one chats, this is the name of the contact.
 * For group chats, this is the name given eg. to dc_create_group_chat() or
 * received by a group-creation message.
 *
 * To change the name, use dc_set_chat_name()
 *
 * See also: dc_chat_get_subtitle()
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return Chat name as a string. Must be free()'d after usage. Never NULL.
 */
char* dc_chat_get_name(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return dc_strdup("Err");
	}

	return dc_strdup(chat->m_name);
}


/**
 * Get a subtitle for a chat.  The subtitle is eg. the email-address or the
 * number of group members.
 *
 * See also: dc_chat_get_name()
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object to calulate the subtitle for.
 *
 * @return Subtitle as a string. Must be free()'d after usage. Never NULL.
 */
char* dc_chat_get_subtitle(dc_chat_t* chat)
{
	/* returns either the address or the number of chat members */
	char*         ret = NULL;

	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return dc_strdup("Err");
	}

	if( chat->m_type == DC_CHAT_TYPE_SINGLE && dc_param_exists(chat->m_param, DC_PARAM_SELFTALK) )
	{
		ret = dc_stock_str(chat->m_context, DC_STR_SELFTALK_SUBTITLE);
	}
	else if( chat->m_type == DC_CHAT_TYPE_SINGLE )
	{
		int r;
		sqlite3_stmt* stmt = dc_sqlite3_prepare(chat->m_context->m_sql,
			"SELECT c.addr FROM chats_contacts cc "
			" LEFT JOIN contacts c ON c.id=cc.contact_id "
			" WHERE cc.chat_id=?;");
		sqlite3_bind_int(stmt, 1, chat->m_id);

		r = sqlite3_step(stmt);
		if( r == SQLITE_ROW ) {
			ret = dc_strdup((const char*)sqlite3_column_text(stmt, 0));
		}

		sqlite3_finalize(stmt);
	}
	else if( DC_CHAT_TYPE_IS_MULTI(chat->m_type) )
	{
		int cnt = 0;
		if( chat->m_id == DC_CHAT_ID_DEADDROP )
		{
			ret = dc_stock_str(chat->m_context, DC_STR_DEADDROP); /* typically, the subtitle for the deaddropn is not displayed at all */
		}
		else
		{
			cnt = dc_get_chat_contact_count(chat->m_context, chat->m_id);
			ret = dc_stock_str_repl_pl(chat->m_context, DC_STR_MEMBER, cnt /*SELF is included in group chats (if not removed)*/);
		}
	}

	return ret? ret : dc_strdup("Err");
}


/**
 * Get the chat's profile image.
 * The profile image is set using dc_set_chat_profile_image() for groups.
 * For normal chats, the profile image is set using dc_set_contact_profile_image() (not yet implemented).
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return Path and file if the profile image, if any.  NULL otherwise.
 *     Must be free()'d after usage.
 */
char* dc_chat_get_profile_image(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return NULL;
	}

	return dc_param_get(chat->m_param, DC_PARAM_PROFILE_IMAGE, NULL);
}


/**
 * Get draft for the chat, if any. A draft is a message that the user started to
 * compose but that is not sent yet. You can save a draft for a chat using dc_set_draft().
 *
 * Drafts are considered when sorting messages and are also returned eg.
 * by dc_chatlist_get_summary().
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return Draft text, must be free()'d. Returns NULL if there is no draft.
 */
char* dc_chat_get_draft(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return NULL;
	}
	return dc_strdup_keep_null(chat->m_draft_text); /* may be NULL */
}



/**
 * Get timestamp of the draft.
 *
 * The draft itself can be get using dc_chat_get_draft().
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return Timestamp of the draft. 0 if there is no draft.
 */
time_t dc_chat_get_draft_timestamp(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return 0;
	}
	return chat->m_draft_timestamp;
}


/**
 * Get archived state.
 *
 * - 0 = normal chat, not archived, not sticky.
 * - 1 = chat archived
 * - 2 = chat sticky (reserved for future use, if you do not support this value, just treat the chat as a normal one)
 *
 * To archive or unarchive chats, use dc_archive_chat().
 * If chats are archived, this should be shown in the UI by a little icon or text,
 * eg. the search will also return archived chats.
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return Archived state.
 */
int dc_chat_get_archived(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return 0;
	}
	return chat->m_archived;
}


/**
 * Check if a chat is still unpromoted.  Chats are unpromoted until the first
 * message is sent.  With unpromoted chats, members can be sent, settings can be
 * modified without the need of special status messages being sent.
 *
 * After the creation with dc_create_group_chat() the chat is usuall  unpromoted
 * until the first call to dc_send_text_msg() or another sending function.
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return 1=chat is still unpromoted, no message was ever send to the chat,
 *     0=chat is not unpromoted, messages were send and/or received
 */
int dc_chat_is_unpromoted(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return 0;
	}
	return dc_param_get_int(chat->m_param, DC_PARAM_UNPROMOTED, 0);
}


/**
 * Check if a chat is verified.  Verified chats contain only verified members
 * and encryption is alwasy enabled.  Verified chats are created using
 * dc_create_group_chat() by setting the 'verified' parameter to true.
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return 1=chat verified, 0=chat is not verified
 */
int dc_chat_is_verified(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return 0;
	}
	return (chat->m_type==DC_CHAT_TYPE_VERIFIED_GROUP);
}


/**
 * Check if a chat is a self talk.  Self talks are normal chats with
 * the only contact DC_CONTACT_ID_SELF.
 *
 * @memberof dc_chat_t
 *
 * @param chat The chat object.
 *
 * @return 1=chat is self talk, 0=chat is no self talk
 */
int dc_chat_is_self_talk(dc_chat_t* chat)
{
	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		return 0;
	}
	return dc_param_exists(chat->m_param, DC_PARAM_SELFTALK);
}


/*******************************************************************************
 * Misc.
 ******************************************************************************/


int dc_chat_update_param(dc_chat_t* chat)
{
	int success = 0;
	sqlite3_stmt* stmt = dc_sqlite3_prepare(chat->m_context->m_sql,
		"UPDATE chats SET param=? WHERE id=?");
	sqlite3_bind_text(stmt, 1, chat->m_param->m_packed, -1, SQLITE_STATIC);
	sqlite3_bind_int (stmt, 2, chat->m_id);
	success = (sqlite3_step(stmt)==SQLITE_DONE)? 1 : 0;
	sqlite3_finalize(stmt);
	return success;
}


static int dc_chat_set_from_stmt(dc_chat_t* chat, sqlite3_stmt* row)
{
	int         row_offset = 0;
	const char* draft_text = NULL;

	if( chat == NULL || chat->m_magic != DC_CHAT_MAGIC || row == NULL ) {
		return 0;
	}

	dc_chat_empty(chat);

	#define CHAT_FIELDS " c.id,c.type,c.name, c.draft_timestamp,c.draft_txt,c.grpid,c.param,c.archived, c.blocked "
	chat->m_id              =                    sqlite3_column_int  (row, row_offset++); /* the columns are defined in CHAT_FIELDS */
	chat->m_type            =                    sqlite3_column_int  (row, row_offset++);
	chat->m_name            =   dc_strdup((char*)sqlite3_column_text (row, row_offset++));
	chat->m_draft_timestamp =                    sqlite3_column_int64(row, row_offset++);
	draft_text              =       (const char*)sqlite3_column_text (row, row_offset++);
	chat->m_grpid           =   dc_strdup((char*)sqlite3_column_text (row, row_offset++));
	dc_param_set_packed(chat->m_param,    (char*)sqlite3_column_text (row, row_offset++));
	chat->m_archived        =                    sqlite3_column_int  (row, row_offset++);
	chat->m_blocked         =                    sqlite3_column_int  (row, row_offset++);

	/* We leave a NULL-pointer for the very usual situation of "no draft".
	Also make sure, m_draft_text and m_draft_timestamp are set together */
	if( chat->m_draft_timestamp && draft_text && draft_text[0] ) {
		chat->m_draft_text = dc_strdup(draft_text);
	}
	else {
		chat->m_draft_timestamp = 0;
	}

	/* correct the title of some special groups */
	if( chat->m_id == DC_CHAT_ID_DEADDROP ) {
		free(chat->m_name);
		chat->m_name = dc_stock_str(chat->m_context, DC_STR_DEADDROP);
	}
	else if( chat->m_id == DC_CHAT_ID_ARCHIVED_LINK ) {
		free(chat->m_name);
		char* tempname = dc_stock_str(chat->m_context, DC_STR_ARCHIVEDCHATS);
			chat->m_name = dc_mprintf("%s (%i)", tempname, dc_get_archived_count(chat->m_context));
		free(tempname);
	}
	else if( chat->m_id == DC_CHAT_ID_STARRED ) {
		free(chat->m_name);
		chat->m_name = dc_stock_str(chat->m_context, DC_STR_STARREDMSGS);
	}
	else if( dc_param_exists(chat->m_param, DC_PARAM_SELFTALK) ) {
		free(chat->m_name);
		chat->m_name = dc_stock_str(chat->m_context, DC_STR_SELF);
	}

	return row_offset; /* success, return the next row offset */
}


/**
 * Library-internal.
 *
 * Calling this function is not thread-safe, locking is up to the caller.
 *
 * @private @memberof dc_chat_t
 *
 * @param chat The chat object that should be filled with the data from the database.
 *     Existing data are free()'d before using dc_chat_empty().
 *
 * @param chat_id Chat ID that should be loaded from the database.
 *
 * @return 1=success, 0=error.
 */
int dc_chat_load_from_db(dc_chat_t* chat, uint32_t chat_id)
{
	int           success = 0;
	sqlite3_stmt* stmt = NULL;

	if( chat==NULL || chat->m_magic != DC_CHAT_MAGIC ) {
		goto cleanup;
	}

	dc_chat_empty(chat);

	stmt = dc_sqlite3_prepare(chat->m_context->m_sql,
		"SELECT " CHAT_FIELDS " FROM chats c WHERE c.id=?;");
	sqlite3_bind_int(stmt, 1, chat_id);

	if( sqlite3_step(stmt) != SQLITE_ROW ) {
		goto cleanup;
	}

	if( !dc_chat_set_from_stmt(chat, stmt) ) {
		goto cleanup;
	}

	success = 1;

cleanup:
	sqlite3_finalize(stmt);
	return success;
}

