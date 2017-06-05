#include "smart_tumbler.h"
#include <recorder.h>
#include <player.h>
#include <bluetooth.h>
#include <dlog.h>
#include <glib.h>

#include <tts.h>

// Http
typedef struct MemoryStruct {
	char *memory;
	size_t size;
} memoryStruct;

typedef struct appdata {
   Evas_Object *win;
   Evas_Object *conform;
   Evas_Object *label;
   Evas_Object *nf;

   Evas_Object *entry;
   Evas_Object *icon;
   memoryStruct ms;

   Evas_Object *btn_rec, *btn_recstop, *btn_play, *btn_playstop;
   player_h player;
   recorder_h recorder;
   recorder_audio_codec_e *codec_list;
   int codec_list_len;
   char file_path[PATH_MAX];
   recorder_audio_codec_e codec;
   recorder_file_format_e file_format;
   FILE *preproc_file;


} appdata_s;

typedef struct {
   recorder_audio_codec_e *codec_list;
   int len;
} supported_encoder_data;

bt_error_e ret;
const char* my_uuid = "00001101-0000-1000-8000-00805F9B34FB";
const char* service_uuid="00001101-0000-1000-8000-00805F9B34FB";
const char* remote_server_name = "server device";
char *bt_server_address = NULL;

Evas_Object *connect_btn;
Evas_Object *btn;
Evas_Object *btn2;
Evas_Object *btn3;

tts_h tts;

GList *devices_list = NULL;

int client_fd = 0;
int mode = -1;




////////////////////// About TTS

static void
add_text(tts_h tts, char* text){
	const char* language = "en_US";
	int voice_type = TTS_VOICE_TYPE_FEMALE;
	int speed = TTS_SPEED_AUTO;
	int utt_id;

	int ret = tts_add_text(tts, text, language, voice_type, speed, &utt_id);
	if( ret != TTS_ERROR_NONE ) {
		dlog_print(DLOG_INFO, LOG_TAG, "%s err = %d(add text failed)", __func__, ret);
	}
}

static int
get_state(tts_h* tts){
	tts_state_e current_state;
	int ret;
	ret = tts_get_state(*tts, &current_state);

	if(ret != TTS_ERROR_NONE) {
		dlog_print(DLOG_INFO, LOG_TAG, "%s state = %d(get state failed)", __func__, ret);
		return -1;
	}
	else{
		dlog_print(DLOG_INFO, LOG_TAG, "%s state = %d", __func__, current_state);
		return (int) current_state;
	}
}


static void
state_changed_cb(tts_h tts, tts_state_e previous, tts_state_e current, void* user_data){

}

static void
utterance_completed_cb(tts_h tts, tts_state_e previous, tts_state_e current, void* user_data){

}

static void
utterance_started_cb(tts_h tts, tts_state_e previous, tts_state_e current, void* user_data){

}

static tts_h
create_tts_handle(){
	tts_h tts;
	int ret = tts_create(&tts);
	if(TTS_ERROR_NONE != ret){
		dlog_print(DLOG_INFO, LOG_TAG, "%s err = %d(tts create fail)", __func__, ret);
	}
	else{
		tts_set_utterance_started_cb(tts, utterance_started_cb, NULL);
		tts_set_utterance_completed_cb(tts, utterance_completed_cb, NULL);
		tts_set_state_changed_cb(tts, state_changed_cb, NULL);
		tts_prepare(tts);
	}

	return tts;
}




static void destroy_tts_handle(tts_h tts){
	int ret = tts_destroy(tts);
	if(ret != TTS_ERROR_NONE){
		dlog_print(DLOG_INFO, LOG_TAG, "%s err = %d", __func__, ret);
	}
}


/////////////////////// bluetooth

bt_adapter_state_e adapter_state;

void
adapter_visibility_mode_changed_cb(int result, bt_adapter_visibility_mode_e visibility_mode, void* user_data)
{
    if (result != BT_ERROR_NONE) {
        /* Error handling */

        return;
    }
    if (visibility_mode == BT_ADAPTER_VISIBILITY_MODE_NON_DISCOVERABLE)
        dlog_print(DLOG_INFO, LOG_TAG, "[visibility_mode_changed_cb] None discoverable mode!");
    else if (visibility_mode == BT_ADAPTER_VISIBILITY_MODE_GENERAL_DISCOVERABLE)
        dlog_print(DLOG_INFO, LOG_TAG, "[visibility_mode_changed_cb] General discoverable mode!");
    else
        dlog_print(DLOG_INFO, LOG_TAG, "[visibility_mode_changed_cb] Limited discoverable mode!");
}

int server_socket_fd = -1;
void
socket_connection_state_changed(int result, bt_socket_connection_state_e connection_state,
                                bt_socket_connection_s *connection, void *user_data)
{
    if (result != BT_ERROR_NONE) {
        dlog_print(DLOG_ERROR, LOG_TAG, "[socket_connection_state_changed_cb] failed. result =%d.", result);

        return;
    }

    if (connection_state == BT_SOCKET_CONNECTED) {
        dlog_print(DLOG_INFO, LOG_TAG, "Callback: Connected.");
        if (connection != NULL) {
            dlog_print(DLOG_INFO, LOG_TAG, "Callback: Socket of connection - %d.", connection->socket_fd);
            dlog_print(DLOG_INFO, LOG_TAG, "Callback: Role of connection - %d.", connection->local_role);
            dlog_print(DLOG_INFO, LOG_TAG, "Callback: Address of connection - %s.", connection->remote_address);
            /* socket_fd is used for sending data and disconnecting a device */
            server_socket_fd = connection->socket_fd;
        } else {
            dlog_print(DLOG_INFO, LOG_TAG, "Callback: No connection data");
        }
    } else {
        dlog_print(DLOG_INFO, LOG_TAG, "Callback: Disconnected.");
        if (connection != NULL) {
            dlog_print(DLOG_INFO, LOG_TAG, "Callback: Socket of disconnection - %d.", connection->socket_fd);
            dlog_print(DLOG_INFO, LOG_TAG, "Callback: Address of connection - %s.", connection->remote_address);
        } else {
            dlog_print(DLOG_INFO, LOG_TAG, "Callback: No connection data");
        }
    }
}

bool
adapter_bonded_device_cb(bt_device_info_s *device_info, void *user_data)
{
    if (device_info == NULL)
        return true;
    if (!strcmp(device_info->remote_name, (char*)user_data)) {
        dlog_print(DLOG_INFO, LOG_TAG, "The server device is found in bonded device list. address(%s)",
                   device_info->remote_address);
        bt_server_address = strdup(device_info->remote_address);
        /* If you want to stop iterating, you can return "false" */
    }
    /* Get information about bonded device */
    int count_of_bonded_device = 1;
    dlog_print(DLOG_INFO, LOG_TAG, "Get information about the bonded device(%d)", count_of_bonded_device);
    dlog_print(DLOG_INFO, LOG_TAG, "remote address = %s.", device_info->remote_address);
    dlog_print(DLOG_INFO, LOG_TAG, "remote name = %s.", device_info->remote_name);
    dlog_print(DLOG_INFO, LOG_TAG, "service count = %d.", device_info->service_count);
    dlog_print(DLOG_INFO, LOG_TAG, "bonded?? %d.", device_info->is_bonded);
    dlog_print(DLOG_INFO, LOG_TAG, "connected?? %d.", device_info->is_connected);
    dlog_print(DLOG_INFO, LOG_TAG, "authorized?? %d.", device_info->is_authorized);

    dlog_print(DLOG_INFO, LOG_TAG, "major_device_class %d.", device_info->bt_class.major_device_class);
    dlog_print(DLOG_INFO, LOG_TAG, "minor_device_class %d.", device_info->bt_class.minor_device_class);
    dlog_print(DLOG_INFO, LOG_TAG, "major_service_class_mask %d.", device_info->bt_class.major_service_class_mask);
    count_of_bonded_device++;

    /* Keep iterating */

    return true;
}

void
bt_received_cb(bt_socket_received_data_s* data, void* user_data)
{
    if (data == NULL) {
        dlog_print(DLOG_INFO, LOG_TAG, "No received data!");

        return;
    }
    dlog_print(DLOG_INFO, LOG_TAG, "Socket fd: %d", data->socket_fd);
    dlog_print(DLOG_INFO, LOG_TAG, "Data: %s", data->data);
    dlog_print(DLOG_INFO, LOG_TAG, "Size: %d", data->data_size);



    const char* language = "en_US";
    int voice_type = TTS_VOICE_TYPE_FEMALE;
    int speed = TTS_SPEED_AUTO;
    char* text;

    /*if(mode == 1){
    	// location
    	text = data->data;

    }
    else if(mode == 2){
    	// tem
    	char str[] = "do";
    	strcat(data->data, str);
    	text = data->data;
    }
    else if(mode == 3){
    	// height
    	text = data->data;
    }*/
    //dlog_print(DLOG_INFO, "tag", "%s", text);

    int state = get_state(&tts);
    if( (tts_state_e) state == TTS_STATE_READY || (tts_state_e) state == TTS_STATE_PAUSED ){
    	add_text(tts, data->data);
    	int ret = tts_play(tts);
    	if(TTS_ERROR_NONE != ret) {
    		dlog_print(DLOG_INFO, LOG_TAG, "%s err = %d", __func__, ret);
    	}
    }
    else if( (tts_state_e) state == TTS_STATE_PLAYING ) {
    	int ret = tts_stop(tts);
    	if(TTS_ERROR_NONE != ret) {
    		dlog_print(DLOG_INFO, LOG_TAG, "%s err = %d", __func__, ret);
    	}
    }


}

static void
btn_connect_callback(void *data, Evas_Object *obj, void *event_info)
{
	bt_adapter_visibility_mode_e mode;
	/* Duration until the visibility mode is changed so that other devices cannot find your device */
	/*int duration = 1;

	bt_adapter_get_visibility(&mode, &duration);
	if (mode == BT_ADAPTER_VISIBILITY_MODE_NON_DISCOVERABLE)
	    dlog_print(DLOG_INFO, LOG_TAG, "The device is not discoverable.");
	else if (mode == BT_ADAPTER_VISIBILITY_MODE_GENERAL_DISCOVERABLE)
	    dlog_print(DLOG_INFO, LOG_TAG, "The device is discoverable. No time limit.");
	else
	    dlog_print(DLOG_INFO, LOG_TAG, "The device is discoverable for a set period of time.");

	ret = bt_socket_listen_and_accept_rfcomm(server_socket_fd, 5);
	if (ret != BT_ERROR_NONE) {
	    dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_listen_and_accept_rfcomm] failed.");

	    return;
	} else {
	    dlog_print(DLOG_INFO, LOG_TAG, "[bt_socket_listen_and_accept_rfcomm] Succeeded. bt_socket_connection_state_changed_cb will be called.");
	    /* Waiting for incoming connections */
	//}

	ret = bt_socket_connect_rfcomm("98:D3:31:FD:46:B9", service_uuid);
	if (ret != BT_ERROR_NONE) {
	    dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_connect_rfcomm] failed.");

	    return;
	} else {
	    dlog_print(DLOG_INFO, LOG_TAG, "[bt_socket_connect_rfcomm] Succeeded. bt_socket_connection_state_changed_cb will be called.");
	}

	int ret = tts_create(&tts);
	if(TTS_ERROR_NONE != ret){
		dlog_print(DLOG_INFO, LOG_TAG, "%s err = %d(tts create fail)", __func__, ret);
	}
	else{
		tts_set_utterance_started_cb(tts, utterance_started_cb, NULL);
		tts_set_utterance_completed_cb(tts, utterance_completed_cb, NULL);
		tts_set_state_changed_cb(tts, state_changed_cb, NULL);
		tts_prepare(tts);
	}

	elm_object_disabled_set(connect_btn, EINA_TRUE);
	elm_object_disabled_set(btn, EINA_FALSE);
	elm_object_disabled_set(btn2, EINA_FALSE);
	elm_object_disabled_set(btn3, EINA_FALSE);

}

static void
btn_temperature_callback(void *data, Evas_Object *obj, void *event_info)
{
   appdata_s *ad = data;
   //elm_object_text_set(ad->label, "Pressed!");
   dlog_print(DLOG_ERROR, LOG_TAG, "temperature.");
   mode = 2;
   char sign[] = "2";

   ret = bt_socket_send_data(server_socket_fd, sign, sizeof(sign));
   if(ret != BT_ERROR_NONE)
	   dlog_print(DLOG_ERROR, LOG_TAG, "send error.");
   else{
	   dlog_print(DLOG_ERROR, LOG_TAG, "bt_socket_send_data() success.");
   }
}

static void
btn_location_callback(void *data, Evas_Object *obj, void *event_info)
{
	appdata_s *ad = data;
	   //elm_object_text_set(ad->label, "Pressed!");
	   dlog_print(DLOG_ERROR, LOG_TAG, "temperature.");
	   mode = 1;
	   char sign[] = "1";

	   ret = bt_socket_send_data(server_socket_fd, sign, sizeof(sign));
	   if(ret != BT_ERROR_NONE)
		   dlog_print(DLOG_ERROR, LOG_TAG, "send error.");
	   else{
		   dlog_print(DLOG_ERROR, LOG_TAG, "bt_socket_send_data() success.");
	   }
}

static void
btn_height_callback(void *data, Evas_Object *obj, void *event_info)
{
	appdata_s *ad = data;
	   //elm_object_text_set(ad->label, "Pressed!");
	   dlog_print(DLOG_ERROR, LOG_TAG, "temperature.");
	   mode = 3;
	   char sign[] = "3";

	   ret = bt_socket_send_data(server_socket_fd, sign, sizeof(sign));
	   if(ret != BT_ERROR_NONE)
		   dlog_print(DLOG_ERROR, LOG_TAG, "send error.");
	   else{
		   dlog_print(DLOG_ERROR, LOG_TAG, "bt_socket_send_data() success.");
	   }
}



//////////////////////// bluetooth end



static void
win_delete_request_cb(void *data, Evas_Object *obj, void *event_info)
{
   ui_app_exit();
}

static void
win_back_cb(void *data, Evas_Object *obj, void *event_info)
{
   appdata_s *ad = data;
   /* Let window go to hide state. */
   elm_win_lower(ad->win);
}

static void
my_box_pack(Evas_Object *box, Evas_Object *child, double h_weight, double v_weight, double h_align, double v_align)
{
   Evas_Object *frame = elm_frame_add(box);
   elm_object_style_set(frame, "pad_medium");
   evas_object_size_hint_weight_set(frame, h_weight, v_weight);
   evas_object_size_hint_align_set(frame, h_align, v_align);
   {
      evas_object_size_hint_weight_set(child, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
      evas_object_size_hint_align_set(child, EVAS_HINT_FILL, EVAS_HINT_FILL);
      evas_object_show(child);
      elm_object_content_set(frame, child);
   }
   elm_box_pack_end(box, frame);
   evas_object_show(frame);
}

/*
   You can get bt_server_address from bt_adapter_bonded_device_cb() or bt_device_service_searched_cb()
   device_info->remote_address in bt_adapter_bonded_device_cb()
   sdp_info->remote_address in bt_device_service_searched_cb()
*/


static void
btn_record_callback(void *data, Evas_Object *obj, void *event_info)
{
   appdata_s *ad = data;
   elm_object_text_set(ad->label, "record!");
}

static Eina_Bool
naviframe_pop_cb(void *data, Elm_Object_Item *it)
{
	ui_app_exit();
	return EINA_FALSE;
}

///// recoder function

// Check is recording
static bool
_recorder_is_recording(appdata_s *ad){
	recorder_state_e state = RECORDER_STATE_NONE;
	recorder_get_state(ad->recorder, &state);
	return state == RECORDER_STATE_RECORDING;
}

// Stop recording
static void
record_stop(void *data, Evas_Object *obj, void *event_info){
	appdata_s *ad = data;
	if(ad->recorder){
		recorder_commit(ad->recorder);
		// Check is recording
		if(!_recorder_is_recording(ad)){
			recorder_unprepare(ad->recorder);
		}
		elm_object_disabled_set(ad->btn_play, EINA_FALSE);
		elm_object_disabled_set(ad->btn_rec, EINA_FALSE);
		elm_object_disabled_set(ad->btn_playstop, EINA_TRUE);
		elm_object_disabled_set(ad->btn_recstop, EINA_TRUE);
	}
}

// Maximum recording time event callback function
static void
_on_recording_limit_reached_cb(recorder_recording_limit_type_e type, void *user_data){
	appdata_s *ad = user_data;
	if(type == RECORDER_RECORDING_LIMIT_TIME){
		// stop recording
		record_stop(ad, NULL, NULL);
	}
}

// Create recorder
static void
_recorder_create(appdata_s *ad){
	if(recorder_create_audiorecorder(&ad->recorder) == RECORDER_ERROR_NONE){
		// Set maximum recording time event callback function
		recorder_set_recording_limit_reached_cb(ad->recorder, _on_recording_limit_reached_cb, ad);
		recorder_attr_set_audio_channel(ad->recorder, 1);
		recorder_attr_set_audio_device(ad->recorder, RECORDER_AUDIO_DEVICE_MIC);
		recorder_attr_set_time_limit(ad->recorder, 20);
	}
}

static bool
_recorder_supported_audio_encoder_cb(recorder_audio_codec_e codec, void *user_data){
	bool result = false;
	supported_encoder_data *data = user_data;

	if (data && codec != RECORDER_AUDIO_CODEC_DISABLE){
		data->codec_list = realloc(data->codec_list, sizeof(supported_encoder_data) * (data->len +1));
		data->codec_list[data->len] = codec;
		++(data->len);
		result = true;
	}

	return result;
}

recorder_audio_codec_e*
audio_recorder_get_supported_encoder(recorder_h recorder, int *list_length){
	supported_encoder_data data = {0};
	data.codec_list = NULL;
	data.len = 0;
	int res = recorder_foreach_supported_audio_encoder(recorder, _recorder_supported_audio_encoder_cb, &data);

	if(res && list_length){
		*list_length = data.len;
	}

	return data.codec_list;
}

const char*
get_file_format_by_codec(appdata_s* ad){
	switch(ad->codec){
	case RECORDER_AUDIO_CODEC_AMR:
		ad->file_format = RECORDER_FILE_FORMAT_AMR;
		return "AMR";
		break;
	case RECORDER_AUDIO_CODEC_AAC:
		ad->file_format = RECORDER_FILE_FORMAT_MP4;
		return "MP4";
		break;
	case RECORDER_AUDIO_CODEC_VORBIS:
		ad->file_format = RECORDER_FILE_FORMAT_OGG;
		return "OGG";
		break;
	}

	ad->file_format = RECORDER_FILE_FORMAT_WAV;
	return "WAV";
}

static void
_codec_set(appdata_s *ad){
	char file_name[NAME_MAX] = {'\0'};
	const char *file_ext = get_file_format_by_codec(ad);

	char *data_path = app_get_data_path();
	snprintf(file_name, NAME_MAX, "record.%s", file_ext);
	snprintf(ad->file_path, PATH_MAX, "%s%s", data_path, file_name);
	free(data_path);
}

// Apply settings to recorder
static void _recorder_apply_settings(appdata_s *ad){
	if(ad->recorder){
		recorder_set_filename(ad->recorder, ad->file_path);
		recorder_set_file_format(ad->recorder, ad->file_format);
		recorder_set_audio_encoder(ad->recorder, ad->codec);
	}
}

//Start recording
static void
record_start(void *data, Evas_Object *obj, void *event_info){
	appdata_s *ad = data;
	if(ad -> recorder){
		_recorder_apply_settings(ad);
		recorder_prepare(ad->recorder);
		recorder_start(ad->recorder);
		elm_object_disabled_set(ad->btn_recstop, EINA_FALSE);
		elm_object_disabled_set(ad->btn_rec, EINA_TRUE);
		elm_object_disabled_set(ad->btn_play, EINA_TRUE);
		elm_object_disabled_set(ad->btn_playstop, EINA_TRUE);
	}
}

// Get player state
static player_state_e get_player_state(player_h player){
	player_state_e state;
	player_get_state(player, &state);
	return state;
}

// stop play
static void
stop_player(void *data, Evas_Object *obj, void *event_info){
	appdata_s *ad = data;
	if(get_player_state(ad->player) == PLAYER_STATE_PLAYING || get_player_state(ad->player) == PLAYER_STATE_PAUSED)
		player_stop(ad->player);

	elm_object_disabled_set(ad->btn_play, EINA_FALSE);
	elm_object_disabled_set(ad->btn_playstop, EINA_TRUE);
	elm_object_disabled_set(ad->btn_rec, EINA_FALSE);
	elm_object_disabled_set(ad->btn_recstop, EINA_TRUE);
}

// Load file to player
static void
prepare_player(appdata_s* ad){
	stop_player(ad, NULL, NULL);
	player_unprepare(ad->player);
	player_set_uri(ad->player, ad->file_path);
	player_prepare(ad->player);
}

// Start play
static void
start_player(void* data, Evas_Object *obj, void *event_info){
	appdata_s *ad = data;
	prepare_player(ad);

	if(get_player_state(ad->player) != PLAYER_STATE_PLAYING){
		player_start(ad->player);

		elm_object_disabled_set(ad->btn_rec, EINA_TRUE);
		elm_object_disabled_set(ad->btn_recstop, EINA_TRUE);
		elm_object_disabled_set(ad->btn_play, EINA_TRUE);
		elm_object_disabled_set(ad->btn_playstop, EINA_FALSE);
	}
}

// Create player
static player_h create_player(){
	player_h player;

	player_create(&player);
	player_set_completed_cb(player, NULL, player);

	return player;
}

//////////////////////////////

///////////////// Http function

static size_t
write_memory_cb(void *contents, size_t size, size_t nmemb, void* userp){
	size_t realsize = size * nmemb;
	memoryStruct *mem = (memoryStruct *)userp;

	mem->memory = realloc(mem->memory, mem->size + realsize+1);
	if(mem->memory == NULL){
		//out of memory
		dlog_print(DLOG_INFO, "tag", "not enough memory (realloc returned NULL)");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	return realsize;
}



///////////////////////////////



static void
create_base_gui(appdata_s *ad)
{
   /* Window */
   /* Create and initialize elm_win.
      elm_win is mandatory to manipulate window. */
   ad->win = elm_win_util_standard_add(PACKAGE, PACKAGE);
   elm_win_autodel_set(ad->win, EINA_TRUE);

   if (elm_win_wm_rotation_supported_get(ad->win)) {
      int rots[4] = { 0, 90, 180, 270 };
      elm_win_wm_rotation_available_rotations_set(ad->win, (const int *)(&rots), 4);
   }

   evas_object_smart_callback_add(ad->win, "delete,request", win_delete_request_cb, NULL);
   eext_object_event_callback_add(ad->win, EEXT_CALLBACK_BACK, win_back_cb, ad);

   /* Conformant */
   /* Create and initialize elm_conformant.
      elm_conformant is mandatory for base gui to have proper size
      when indicator or virtual keypad is visible. */
   ad->conform = elm_conformant_add(ad->win);
   elm_win_indicator_mode_set(ad->win, ELM_WIN_INDICATOR_SHOW);
   elm_win_indicator_opacity_set(ad->win, ELM_WIN_INDICATOR_OPAQUE);
   evas_object_size_hint_weight_set(ad->conform, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(ad->win, ad->conform);
   evas_object_show(ad->conform);

   {
	   /* Naviframe */
	   ad->nf = elm_naviframe_add(ad->conform);
	   elm_object_part_content_set(ad->conform, "elm.swallow.content", ad->nf);
	   elm_object_content_set(ad->conform, ad->nf);

      /*Evas_Object *box = elm_box_add(ad->win);
      evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
      elm_object_content_set(ad->conform, box);
      evas_object_show(box);*/

	   Evas_Object* box = elm_box_add(ad->nf);
	   elm_box_padding_set(box, 10*elm_config_scale_get(), 10*elm_config_scale_get());
	   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	   elm_object_content_set(ad->nf, box);
	   evas_object_show(box);
      {
         /* Label */
         ad->label = elm_label_add(ad->conform);
         elm_object_text_set(ad->label, "<align=center>Press a Button</>");
         my_box_pack(box, ad->label, 1.0, 0.0, -1.0, 0.5);

         /* connect but */
         connect_btn = elm_button_add(ad->conform);
         elm_object_text_set(connect_btn, "Connect");
         evas_object_smart_callback_add(connect_btn, "clicked", btn_connect_callback, ad);
         my_box_pack(box, connect_btn, 1.0, 1.0, -1.0, -1.0);


         /* Button 1 */
         btn = elm_button_add(ad->conform);
         elm_object_text_set(btn, "Temperature");
         elm_object_disabled_set(btn, EINA_TRUE);
         evas_object_smart_callback_add(btn, "clicked", btn_temperature_callback, ad);
         my_box_pack(box, btn, 1.0, 1.0, -1.0, -1.0);

         /* Button 2 */
         btn2 = elm_button_add(ad->conform);
         elm_object_text_set(btn2, "Height");
         elm_object_disabled_set(btn2, EINA_TRUE);
         evas_object_smart_callback_add(btn2, "clicked", btn_height_callback, ad);
         my_box_pack(box, btn2, 1.0, 1.0, -1.0, -1.0);

         /* Button 3 */
         btn3 = elm_button_add(ad->conform);
         elm_object_text_set(btn3, "Location");
         elm_object_disabled_set(btn3, EINA_TRUE);
         evas_object_smart_callback_add(btn3, "clicked", btn_location_callback, ad);
         my_box_pack(box, btn3, 1.0, 1.0, -1.0, -1.0);

         /* Button 4 */
         /*Evas_Object *btn4 = elm_button_add(ad->conform);
         elm_object_text_set(btn4, "Record");
         evas_object_smart_callback_add(btn4, "clicked", sub_view_cb, ad->nf);
         my_box_pack(box, btn4, 1.0, 1.0, -1.0, -1.0);
         */

         //record button
         Evas_Object *button, *frame, *tbl;

         /* Frame for somt outer padding */
		 //frame = elm_frame_add(ad->conform);
		 //elm_object_style_set(frame, "pad_medium");
		 //elm_object_content_set(ad->conform, frame);
		 //evas_object_show(frame);

		 /* Table to pack our elements */
		 //tbl = elm_table_add(ad->conform);
		 //elm_table_padding_set(tbl, 5 * elm_scale_get(), 5 * elm_scale_get());
		 //elm_object_content_set(ad->conform, tbl);
		 //evas_object_show(tbl);

		 /*{
			 //ad->label = elm_label_add(tbl);
			 //elm_object_text_set(ad->label, "Audio recorder");
			 //evas_object_size_hint_align_set(ad->label, 0.5, 0.5);
			 //evas_object_size_hint_weight_set(ad->label, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			 //elm_table_pack(tbl, ad->label, 0, 0, 2, 1);
			 //evas_object_show(ad->label);

			 // Record start but
			 button = elm_button_add(ad->conform);
			 elm_object_text_set(button, "Recording Start");
			 evas_object_smart_callback_add(button, "clicked", record_start, (void*)ad);
			 //evas_object_size_hint_weight_set(button, EVAS_HINT_EXPAND, 0);
			 //evas_object_size_hint_align_set(button, EVAS_HINT_FILL, 0.5);
			 //elm_table_pack(tbl, button, 0, 1, 1, 1);
			 my_box_pack(box, button, 1.0, 1.0, -1.0, -1.0);
			 evas_object_show(button);
			 ad->btn_rec = button;

			 // Record stop button
			 button = elm_button_add(ad->conform);
			 elm_object_disabled_set(button, EINA_TRUE);
			 elm_object_text_set(button, "Recording Stop");
			 evas_object_smart_callback_add(button, "clicked", record_stop, (void*)ad);
			 //evas_object_size_hint_weight_set(button, EVAS_HINT_EXPAND, 0);
			 //evas_object_size_hint_align_set(button, EVAS_HINT_FILL, 0.5);
			 //elm_table_pack(tbl, button, 1, 1, 1, 1);
			 my_box_pack(box, button, 1.0, 1.0, -1.0, -1.0);
			 evas_object_show(button);
			 ad->btn_recstop = button;

			 // Play Start Button
			 button = elm_button_add(ad->conform);
			 elm_object_disabled_set(button, EINA_TRUE);
			 elm_object_text_set(button, "Play Start");
			 evas_object_smart_callback_add(button, "clicked", start_player, (void*)ad);
			 //evas_object_size_hint_weight_set(button, EVAS_HINT_FILL, 0.0);
			 //elm_table_pack(tbl, button, 0, 2, 1, 1);
			 my_box_pack(box, button, 1.0, 1.0, -1.0, -1.0);
			 evas_object_show(button);
			 ad->btn_play = button;

			 // Play Stop Button
			 button = elm_button_add(ad->conform);
			 elm_object_disabled_set(button, EINA_TRUE);
			 elm_object_text_set(button, "Play Stop");
			 evas_object_smart_callback_add(button, "clicked", stop_player, (void*)ad);
			 //evas_object_size_hint_weight_set(button, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			 //evas_object_size_hint_align_set(button, EVAS_HINT_FILL, 0.0);
			 //elm_table_pack(tbl, button, 1, 2, 1, 1);
			 my_box_pack(box, button, 1.0, 1.0, -1.0, -1.0);
			 evas_object_show(button);
			 ad->btn_playstop = button;
		 }*/


         /* Header */
         Elm_Object_Item *nf_it;
         nf_it = elm_naviframe_item_push(ad->nf, "Main Window", NULL, NULL, box, NULL);
         eext_object_event_callback_add(ad->nf, EEXT_CALLBACK_BACK, eext_naviframe_back_cb, NULL);
         elm_naviframe_item_pop_cb_set(nf_it, naviframe_pop_cb, ad->win);
      }

   }

   /* Label */
   /* Create an actual view of the base gui.
      Modify this part to change the view. */
   /*ad->label = elm_label_add(ad->conform);
   elm_object_text_set(ad->label, "<align=center>Hello Tizen</align>");
   evas_object_size_hint_weight_set(ad->label, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_object_content_set(ad->conform, ad->label);*/

   // create player
   ad->player = create_player();


   /* Show window after base gui is set up */
   evas_object_show(ad->win);



   //Create Recorder
   _recorder_create(ad);
   ad->codec_list = audio_recorder_get_supported_encoder(ad->recorder, &ad->codec_list_len);
   ad->codec = ad->codec_list_len ? ad->codec_list[0] : RECORDER_AUDIO_CODEC_PCM;
   _codec_set(ad);


   //tts = create_tts_handle();
}

static bool
app_create(void *data)
{
   /* Hook to take necessary actions before main event loop starts
      Initialize UI resources and application's data
      If this function returns true, the main loop of application starts
      If this function returns false, the application is terminated */
   appdata_s *ad = data;

   create_base_gui(ad);

   return true;
}

static void
app_control(app_control_h app_control, void *data)
{
   /* Handle the launch request. */
}

static void
app_pause(void *data)
{
   /* Take necessary actions when application becomes invisible. */
}

static void
app_resume(void *data)
{
   /* Take necessary actions when application becomes visible. */
}

static void
app_terminate(void *data)
{
   /* Release all resources. */
	destroy_tts_handle(tts);
}

static void
ui_app_lang_changed(app_event_info_h event_info, void *user_data)
{
   /*APP_EVENT_LANGUAGE_CHANGED*/

   int ret;
   char *language;

   ret = app_event_get_language(event_info, &language);
   if (ret != APP_ERROR_NONE) {
      dlog_print(DLOG_ERROR, LOG_TAG, "app_event_get_language() failed. Err = %d.", ret);
      return;
   }

   if (language != NULL) {
      elm_language_set(language);
      free(language);
   }
}

static void
ui_app_orient_changed(app_event_info_h event_info, void *user_data)
{
   /*APP_EVENT_DEVICE_ORIENTATION_CHANGED*/
   return;
}

static void
ui_app_region_changed(app_event_info_h event_info, void *user_data)
{
   /*APP_EVENT_REGION_FORMAT_CHANGED*/
}

static void
ui_app_low_battery(app_event_info_h event_info, void *user_data)
{
   /*APP_EVENT_LOW_BATTERY*/
}

static void
ui_app_low_memory(app_event_info_h event_info, void *user_data)
{
   /*APP_EVENT_LOW_MEMORY*/
}



int
main(int argc, char *argv[])
{
   appdata_s ad = {0,};
   int app_ret = 0;

   ui_app_lifecycle_callback_s event_callback = {0,};
   app_event_handler_h handlers[5] = {NULL, };

   event_callback.create = app_create;
   event_callback.terminate = app_terminate;
   event_callback.pause = app_pause;
   event_callback.resume = app_resume;
   event_callback.app_control = app_control;

   // setting bluetooth
   ret = bt_initialize();
   if (ret != BT_ERROR_NONE) {
       dlog_print(DLOG_ERROR, LOG_TAG, "[bt_initialize] failed.");

       return 0;
   }
   else {
	   dlog_print(DLOG_ERROR, LOG_TAG, "[bt_initialize] success.");
   }

   ret = bt_socket_create_rfcomm(my_uuid, &server_socket_fd);
   if (ret != BT_ERROR_NONE)
       dlog_print(DLOG_ERROR, LOG_TAG, "bt_socket_create_rfcomm() failed.");
   else
	   dlog_print(DLOG_ERROR, LOG_TAG, "bt_socket_create_rfcomm() success.");

   ret = bt_socket_set_connection_state_changed_cb(socket_connection_state_changed, NULL);
   if (ret != BT_ERROR_NONE) {
       dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_set_connection_state_changed_cb] failed.");

       return 0;
   }
   else{
	   dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_set_connection_state_changed_cb] success.");
   }

   ret = bt_adapter_foreach_bonded_device(adapter_bonded_device_cb, remote_server_name);
   if (ret != BT_ERROR_NONE)
       dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_foreach_bonded_device] failed!");
   else{
	   dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_foreach_bonded_device] success");
   }

   if (bt_server_address != NULL)
       free(bt_server_address);

   ret = bt_adapter_set_visibility_mode_changed_cb(adapter_visibility_mode_changed_cb, NULL);
   if (ret != BT_ERROR_NONE)
       dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_set_visibility_mode_changed_cb] failed.");
   else
	   dlog_print(DLOG_ERROR, LOG_TAG, "[bt_adapter_set_visibility_mode_changed_cb] success.");

	 ret = bt_socket_set_data_received_cb(bt_received_cb, NULL);
	   if (ret != BT_ERROR_NONE)
	       dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_data_received_cb] regist failed.");
	   else{
		   dlog_print(DLOG_ERROR, LOG_TAG, "[bt_socket_data_received_cb] regist success.");
	   }

   // setting bluetooth end




   ui_app_add_event_handler(&handlers[APP_EVENT_LOW_BATTERY], APP_EVENT_LOW_BATTERY, ui_app_low_battery, &ad);
   ui_app_add_event_handler(&handlers[APP_EVENT_LOW_MEMORY], APP_EVENT_LOW_MEMORY, ui_app_low_memory, &ad);
   ui_app_add_event_handler(&handlers[APP_EVENT_DEVICE_ORIENTATION_CHANGED], APP_EVENT_DEVICE_ORIENTATION_CHANGED, ui_app_orient_changed, &ad);
   ui_app_add_event_handler(&handlers[APP_EVENT_LANGUAGE_CHANGED], APP_EVENT_LANGUAGE_CHANGED, ui_app_lang_changed, &ad);
   ui_app_add_event_handler(&handlers[APP_EVENT_REGION_FORMAT_CHANGED], APP_EVENT_REGION_FORMAT_CHANGED, ui_app_region_changed, &ad);

   app_ret = ui_app_main(argc, argv, &event_callback, &ad);
   if (app_ret != APP_ERROR_NONE) {
      dlog_print(DLOG_ERROR, LOG_TAG, "app_main() is failed. err = %d", ret);
   }

   return app_ret;
}
