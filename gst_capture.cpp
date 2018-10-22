#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gst_capture.h"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/gstclock.h>
#include <X11/Xlib.h>
#include <sys/time.h>

#include "osa.h"
#include "osa_image_queue.h"
#include "osa_sem.h"

typedef struct _CustomData
{
  	GstElement *pipeline, *source, *videoconvert0, *tee0, *queue0, *fakesink0;
  	GstElement *queue1, *nvvidconv0, *omxh265enc, *fakesink1, *rtph265pay,*clockoverlay, *udpsink;
  	GstElement *queue3, *filesink2;

	GstBus *bus;
	GMainLoop *loop;
	gboolean playing;
	gboolean source_screen;

	GstStateChangeReturn ret;
	GstCaps *caps_src_to_convert;
  	GstCaps *caps_enc_to_rtp;
  	GstCaps *caps_nvconv_to_enc;

	GstPad *nvvidconv0_srcpad;
	GstPad *videoconvert0_srcpad;
	GstPadTemplate *tee0_src_pad_template;
	GstPad *tee0queue0_srcpad;
	GstPad *tee0queue1_srcpad;

	int height;
	int width;
	int framerate;
	int bitrate;
	char format[30];
	char ip_addr[30];

	gchar* filename_format;
	int tempfileNum;
	int filp_method;
	int capture_src;

	int testCount;

	unsigned int  port;
	void *hdlSink;
	RecordHandle *record;
	GstClockTime buffer_timestamp;

	OSA_BufHndl pushBuffQueue;
	pthread_t threadPushBuffer;
	OSA_SemHndl pushSem;
	bool bPush;

	OSA_BufHndl outBuffQueue;
	pthread_t threadOutBuffer;
	pthread_t threadTimer;
	OSA_SemHndl *outSem;
	void *notify;
	bool bOut;
	int outDgFlag;
	unsigned long counts[8];
} CustomData;

#if 0
static GstPadProbeReturn enc_buffer(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    CustomData *pData = (CustomData *)user_data;
    //gint iBufSize = 0;
    //gchar* pbuffer = NULL;
    GstMapInfo map;
    GstBuffer *buffer;

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    if(buffer == NULL)
        return GST_PAD_PROBE_OK;

    //iBufSize = gst_buffer_get_size(buffer);

    if(gst_buffer_map(buffer, &map, GST_MAP_READ))
    {
        //vidgst_dataTransfer(map.data, map.size);
    	//send 422
    	GstClockTime tm = GST_BUFFER_PTS(buffer);
    	//g_print("%s %d: encoder%d tm %ld\n", __func__, __LINE__, pData->record->index, tm);
    	if( pData->record->sd_cb!=NULL)
    		pData->record->sd_cb(pData->record->index, map.data, map.size);
        gst_buffer_unmap(buffer, &map);
    }

    //GST_PAD_PROBE_INFO_DATA(info) = buffer;

    return GST_PAD_PROBE_OK;  //just into print one time
}
#else
static void * thrdhndl_timer(void* arg)
{
	CustomData* pData = (CustomData *)arg;
	struct timeval timerStart, timerLast;
	unsigned long ulCount = 0;
	gettimeofday(&timerStart, NULL);
	timerLast = timerStart;
	while(pData->bOut){
		OSA_semSignal(pData->outSem);
		pData->counts[0] ++;
		ulCount ++;
		bool bSync = false;
		struct timeval timerStop, timerElapsed;
		gettimeofday(&timerStop, NULL);
		timersub(&timerStop, &timerStart, &timerElapsed);
		double ms = (timerElapsed.tv_sec*1000.0+timerElapsed.tv_usec/1000.0);
		double nms = 1000.0/pData->framerate*ulCount;
		double wms = nms-ms-0.5;
		/*if(pData->record->index == 0){
			timersub(&timerStop, &timerLast, &timerElapsed);
			g_print("%s %d: %ld.%ld\n", __func__, __LINE__, timerElapsed.tv_sec, timerElapsed.tv_usec);
			timerLast = timerStop;
		}*/
		if(wms<0.000001 || wms>1000.0/pData->framerate+5.0){
			bSync = true;
			g_print("%s %d: tm sync!! \n", __func__, __LINE__);
		}
		if(bSync){
			gettimeofday(&timerStart, NULL);
			ulCount = 0;
		}else{
			struct timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = wms*1000.0;
			select( 0, NULL, NULL, NULL, &timeout );
		}
	}
	return NULL;
}

static void * thrdhndl_out_buffer(void* arg)
{
	CustomData* pData = (CustomData *)arg;
	pData->bOut = true;
	while(pData->bOut){
		OSA_semWait(pData->outSem, OSA_TIMEOUT_FOREVER);
		if(!pData->bOut)
			break;
		pData->counts[1] ++;
		pData->outDgFlag = 1;
		OSA_BufInfo* bufInfo = image_queue_getFull(&pData->outBuffQueue);
		GstBuffer *buffer;
	    GstMapInfo map;
		if(bufInfo != NULL){
			buffer = (GstBuffer *)bufInfo->physAddr;
			OSA_assert(buffer != NULL);
		    if(gst_buffer_map(buffer, &map, GST_MAP_READ))
		    {
		    	pData->outDgFlag = 2;
		    	if( pData->record->sd_cb!=NULL)
		    		pData->record->sd_cb(pData->record->index, map.data, map.size);
		    	pData->outDgFlag = 3;
		        gst_buffer_unmap(buffer, &map);
		        pData->outDgFlag = 4;
		    }
		    gst_buffer_unref(buffer);
			bufInfo->physAddr = NULL;
			image_queue_putEmpty(&pData->outBuffQueue, bufInfo);
			pData->outDgFlag = 5;
		}
		int cnt;
		if((cnt = image_queue_fullCount(&pData->outBuffQueue))>1){
			pData->outDgFlag = 6;
			//g_print("%s %d: queue full count = %d\n", __func__, __LINE__, cnt);
			OSA_semSignal(pData->outSem);
			pData->outDgFlag = 7;
		}
		pData->outDgFlag = 0;
	}
	return NULL;
}
//static GstPadProbeReturn enc_buffer_scheduler(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
static GstPadProbeReturn enc_buffer(GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
    CustomData *pData = (CustomData *)user_data;
    GstBuffer *buffer;

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if(buffer == NULL)
        return GST_PAD_PROBE_OK;

    pData->counts[2] ++;

    OSA_BufInfo* bufInfo = image_queue_getEmpty(&pData->outBuffQueue);
    if(bufInfo == NULL){
    	g_print("%s %d: WARN encoder%d overflow !(flag = %d) %ld %ld %ld\n",
    			__func__, __LINE__, pData->record->index, pData->outDgFlag,
    			pData->counts[0],pData->counts[1],pData->counts[2]);
    	return GST_PAD_PROBE_OK;
    }
    bufInfo->physAddr = buffer;
	struct timeval tmCur;
	gettimeofday(&tmCur, NULL);
    bufInfo->timestamp = (uint64_t)tmCur.tv_sec*1000000000ul + (uint64_t)tmCur.tv_usec*1000ul;
    bufInfo->timestampCap = GST_BUFFER_PTS(buffer);
    gst_buffer_ref(buffer);
    image_queue_putFull(&pData->outBuffQueue, bufInfo);
	if(pData->threadOutBuffer == 0){
		pthread_create(&pData->threadOutBuffer, NULL, thrdhndl_out_buffer, (void*)pData);
		OSA_assert(pData->threadOutBuffer != 0);
	}
	if(pData->threadTimer == 0 && pData->notify == NULL){
		pthread_create(&pData->threadTimer, NULL, thrdhndl_timer, (void*)pData);
		OSA_assert(pData->threadTimer != 0);
	}

    return GST_PAD_PROBE_OK;  //just into print one time
}
#endif
static GstPadProbeReturn enc_tick_cb(GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	CustomData *data = (CustomData *)user_data;
	GstPad *sinkpad, *srcpad;

	if(!GST_IS_ELEMENT (data->omxh265enc))
	{
		g_print ("enable record\n");
	}
	else
	{
		g_print ("already enable record\n");
		return GST_PAD_PROBE_REMOVE;
	}

	//data->nvvidconv0 = gst_element_factory_make ("nvvidconv", NULL);
    data->omxh265enc = gst_element_factory_make ("omxh265enc", NULL);
	data->queue1 = gst_element_factory_make("queue", NULL);
	data->udpsink = gst_element_factory_make("udpsink", NULL);
    data->rtph265pay = gst_element_factory_make("rtph265pay", NULL);

	gst_object_ref(data->queue1);
	//gst_object_ref(data->nvvidconv0);
    gst_object_ref(data->omxh265enc);
    gst_object_ref(data->rtph265pay);
	gst_object_ref(data->udpsink);

   // gst_bin_add_many (GST_BIN(data->pipeline), data->queue1, data->nvvidconv0, data->omxh265enc, data->rtph265pay, data->udpsink,  NULL);
    gst_bin_add_many (GST_BIN(data->pipeline), data->queue1, data->omxh265enc, data->rtph265pay, data->udpsink,  NULL);

	GstPad *queue1_sinkpad = gst_element_get_static_pad (data->queue1, "sink");
	if (gst_pad_link (data->tee0queue1_srcpad, queue1_sinkpad) != GST_PAD_LINK_OK)
	{
		g_printerr ("data->Tee0 could not be linked to data->queue1_sinkpad.\n");
		gst_object_unref (queue1_sinkpad);
		return GST_PAD_PROBE_REMOVE;
	}
	gst_object_unref (queue1_sinkpad);

 //   if(!gst_element_link_many(data->queue1, data->nvvidconv0, data->omxh265enc, NULL))
    	  if(!gst_element_link_many(data->queue1, data->omxh265enc, NULL))
	{
        g_printerr ("Elements could not be linked:data->nvvidconv0--->data->omxh265enc.\n");
		gst_object_unref (data->pipeline);
		return (GstPadProbeReturn)-1;
	}

    if(!gst_element_link_filtered(data->omxh265enc, data->rtph265pay, data->caps_enc_to_rtp))
	{
		g_printerr ("Elements could not be linked.\n");
		gst_object_unref (data->pipeline);
		return (GstPadProbeReturn)-1;
	}

    if(!gst_element_link_many(data->rtph265pay, data->udpsink, NULL))
	{
        g_printerr ("Elements could not be linked:data->rtph265pay ---> data->udpsink.\n");
		gst_object_unref (data->pipeline);
		return (GstPadProbeReturn)-1;
	}

    g_object_set (data->omxh265enc, "iframeinterval", data->framerate, NULL);
    g_object_set (data->omxh265enc, "bitrate", data->bitrate, NULL);

    g_object_set (data->rtph265pay, "config-interval", 1, NULL);
	g_object_set (data->udpsink, "host", data->ip_addr, NULL);
	g_object_set (data->udpsink, "port", data->port, NULL);

    GstPad *h265enc_pad = gst_element_get_static_pad(data->omxh265enc,"src");
    gst_pad_add_probe (h265enc_pad, GST_PAD_PROBE_TYPE_BUFFER,(GstPadProbeCallback) enc_buffer, data, NULL);
    gst_object_unref(h265enc_pad);

	gst_element_sync_state_with_parent (data->queue1);
//	gst_element_sync_state_with_parent (data->nvvidconv0);
    gst_element_sync_state_with_parent (data->omxh265enc);

    gst_element_sync_state_with_parent (data->rtph265pay);
	gst_element_sync_state_with_parent (data->udpsink);

	g_print ("\nEnable record done\n");

	return GST_PAD_PROBE_REMOVE;
}

static GstPadProbeReturn enc_unlink_cb(GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
	CustomData *data = (CustomData *)user_data;
	GstPad *srcpad, *sinkpad;

    if(!GST_IS_ELEMENT (data->omxh265enc))
	{
		return GST_PAD_PROBE_REMOVE;
	}

	g_print (">>> disable record ...\n");

	GST_DEBUG_OBJECT(pad,"pad is blocked now");
	/* remove the probe first. */
	gst_pad_remove_probe(pad, GST_PAD_PROBE_INFO_ID(info));

	g_print(">>> unlinking ...\n" );

	sinkpad = gst_element_get_static_pad(data->queue1, "sink");
	gst_pad_unlink (data->tee0queue1_srcpad, sinkpad);
	gst_object_unref (sinkpad);


	srcpad = gst_element_get_static_pad(data->queue1, "src");
//	sinkpad = gst_element_get_static_pad(data->nvvidconv0, "sink");
	gst_pad_unlink (srcpad, sinkpad);
	gst_object_unref (sinkpad);
	gst_object_unref (srcpad);

//	srcpad = gst_element_get_static_pad(data->nvvidconv0, "src");
    sinkpad = gst_element_get_static_pad(data->omxh265enc, "sink");
	gst_pad_unlink (srcpad, sinkpad);
	gst_object_unref (sinkpad);
	gst_object_unref (srcpad);

    srcpad = gst_element_get_static_pad(data->omxh265enc, "src");
    sinkpad = gst_element_get_static_pad(data->rtph265pay, "sink");
	gst_pad_unlink (srcpad, sinkpad);
	gst_object_unref (sinkpad);
	gst_object_unref (srcpad);

    srcpad = gst_element_get_static_pad(data->rtph265pay, "src");
	sinkpad = gst_element_get_static_pad(data->udpsink, "sink");
	gst_pad_unlink (srcpad, sinkpad);
	gst_object_unref (sinkpad);
	gst_object_unref (srcpad);


	g_print(">>> setting state null ...\n" );
	gst_element_set_state (data->queue1, GST_STATE_NULL);
//	gst_element_set_state (data->nvvidconv0, GST_STATE_NULL);
    gst_element_set_state (data->omxh265enc, GST_STATE_NULL);
    gst_element_set_state (data->rtph265pay, GST_STATE_NULL);
	gst_element_set_state (data->udpsink, GST_STATE_NULL);


	g_print(">>> removing ...\n" );
	gst_bin_remove (GST_BIN (data->pipeline), data->queue1);
//	gst_bin_remove (GST_BIN (data->pipeline), data->nvvidconv0);
    gst_bin_remove (GST_BIN (data->pipeline), data->omxh265enc);
    gst_bin_remove (GST_BIN (data->pipeline), data->rtph265pay);
	gst_bin_remove (GST_BIN (data->pipeline), data->udpsink);

	g_print(">>> unref ...\n" );
	gst_object_unref (data->queue1);
//	gst_object_unref (data->nvvidconv0);
    gst_object_unref (data->omxh265enc);
    gst_object_unref (data->rtph265pay);
	gst_object_unref (data->udpsink);
//    if(data->caps_enc_to_rtp_265 != NULL)
//        gst_caps_unref(data->caps_enc_to_rtp_265);


	data->udpsink = NULL;
    data->rtph265pay = NULL;
    data->omxh265enc = NULL;
	data->nvvidconv0 = NULL;
	data->queue1 = NULL;
//    data->caps_enc_to_rtp_265 = NULL;

	return GST_PAD_PROBE_OK;
}

/* Bus messages processing, similar to all gstreamer examples */

gboolean bus_call(GstBus *bus, GstMessage *message, gpointer user_data)
{
	GMainLoop *loop = (GMainLoop *)user_data;

	switch (GST_MESSAGE_TYPE (message))
	{
		case GST_MESSAGE_ERROR:
			{
				GError *err = NULL;
				gchar *name, *debug = NULL;
				name = gst_object_get_path_string (message->src);
				gst_message_parse_error (message, &err, &debug);
				g_printerr ("ERROR: from element %s: %s\n", name, err->message);
				if (debug != NULL)
					g_printerr ("Additional debug info:\n%s\n", debug);
				g_error_free (err);
				g_free (debug);
				g_free (name);
				g_main_loop_quit (loop);
				break;
			}
		case GST_MESSAGE_WARNING:
			{
				GError *err = NULL;
				gchar *name, *debug = NULL;
				name = gst_object_get_path_string (message->src);
				gst_message_parse_warning (message, &err, &debug);
				g_printerr ("ERROR: from element %s: %s\n", name, err->message);
				if (debug != NULL)
					g_printerr ("Additional debug info:\n%s\n", debug);
				g_error_free (err);
				g_free (debug);
				g_free (name);
				break;
			}
		case GST_MESSAGE_EOS:
			g_print ("Got EOS\n");
			g_main_loop_quit (loop);
			break;

		default:
			break;
	}
	return TRUE;
}

static void getDisplayHeight(int *width, int *height)
{
	char *display_name = getenv("DISPLAY");
	Display* display = XOpenDisplay(display_name);
	int screen_num = DefaultScreen(display);
	*width = DisplayWidth(display, screen_num);
	*height = DisplayHeight(display, screen_num);
	printf("\nscreen w=%d , h=%d\n", *width, *height);
}

int gstlinkInit_convert_enc_fakesink(RecordHandle *recordHandle)
{
	int ret =0;

	CustomData* pData = (CustomData* )recordHandle->context;
	if(pData == NULL)
	{
		printf("CustomData malloc failed.\n");
		return -1;
	}

	//创建空的管道
	char test_pipeline[16]={};
	sprintf(test_pipeline,"test_pipeline_%d",recordHandle->index);
	pData->pipeline = gst_pipeline_new (test_pipeline);

  	//创建元件
	if(XIMAGESRC ==pData->capture_src )
	{
		pData->source = gst_element_factory_make ("ximagesrc", NULL);
		getDisplayHeight( &(pData->width), &(pData->height));
	}
	else if(APPSRC == pData->capture_src )
	{
		pData->source = gst_element_factory_make ("appsrc", NULL);
	}

	if(XIMAGESRC ==pData->capture_src )
	{
		g_object_set(pData->source, "use-damage", 0, NULL);
	}
	else if(APPSRC == pData->capture_src )
	{
		pData->caps_src_to_convert = gst_caps_new_simple("video/x-raw",
										"format", G_TYPE_STRING, pData->format,
										"width", G_TYPE_INT, pData->width,
										"height", G_TYPE_INT, pData->height,
										"framerate", GST_TYPE_FRACTION, pData->framerate*2 ,
										1,
										 NULL);
		/*printf("caps_src_to_convert = %s\n", gst_caps_to_string(pData->caps_src_to_convert));
		char * capsStr = g_strdup_printf("video/x-raw(memory:NVMM),width=(int)%d,height=(int)%d,alignment=(string)au,format=(string)I420,framerate=(fraction)%d/1,pixel-aspect-ratio=(fraction)1/1", pData->width, pData->height, pData->framerate*2);
		pData->caps_nvconv_to_enc = gst_caps_from_string(capsStr);
		g_free(capsStr);*/

		g_object_set(G_OBJECT(pData->source), "caps", pData->caps_src_to_convert, NULL);
		g_object_set(G_OBJECT(pData->source),
					"stream-type", 0,
					"is-live", TRUE,
					//"block", TRUE,
					"do-timestamp", TRUE,
					"format", GST_FORMAT_TIME, NULL);
	}

	pData->caps_enc_to_rtp = gst_caps_new_simple("video/x-h265",
							"stream-format", G_TYPE_STRING, "byte-stream",
							"width", G_TYPE_INT, pData->width,
							"height", G_TYPE_INT, pData->height,
							"framerate", GST_TYPE_FRACTION, pData->framerate*2, 1,
							 NULL);

	pData->videoconvert0 = gst_element_factory_make ("nvvidconv", NULL);
	pData->omxh265enc = gst_element_factory_make ("omxh265enc", NULL);
	pData->fakesink0  = gst_element_factory_make("fakesink", NULL);

	if (!pData->pipeline || !pData->source || !pData->videoconvert0  )
	{
		g_printerr ("Not all elements could be created.\n");
		return -1;
    }

	gst_bin_add_many (GST_BIN(pData->pipeline), pData->source,	pData->videoconvert0, pData->omxh265enc, pData->fakesink0,  NULL);
	if(!gst_element_link_many(pData->source,
			pData->videoconvert0, pData->omxh265enc, NULL))
	{
		g_printerr ("Elements could not be linked:data.source->data0.videoconvert0.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

    if(!gst_element_link_filtered(pData->omxh265enc, pData->fakesink0, pData->caps_enc_to_rtp))
	{
		g_printerr ("Elements could not be linked.\n");
		gst_object_unref (pData->pipeline);
		return (GstPadProbeReturn)-1;
	}

    g_object_set (pData->omxh265enc, "iframeinterval", pData->framerate*2, NULL);
    g_object_set (pData->omxh265enc, "bitrate", pData->bitrate, NULL);

    GstPad *h265enc_pad = gst_element_get_static_pad(pData->omxh265enc,"src");
    gst_pad_add_probe (h265enc_pad, GST_PAD_PROBE_TYPE_BUFFER,(GstPadProbeCallback) enc_buffer, pData, NULL);
    gst_object_unref(h265enc_pad);

	gst_element_sync_state_with_parent (pData->source);
	gst_element_sync_state_with_parent (pData->videoconvert0);
    gst_element_sync_state_with_parent (pData->omxh265enc);
	gst_element_sync_state_with_parent (pData->fakesink0);

	/* Create gstreamer loop */
	pData->loop = g_main_loop_new(NULL, FALSE);
	pData->ret = gst_element_set_state (pData->pipeline, GST_STATE_PLAYING);
	if (pData->ret == GST_STATE_CHANGE_FAILURE)
	{
		g_printerr ("Unable to set the data.pipeline to the playing state.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

  	/* Wait until error or EOS */
	pData->bus = gst_element_get_bus(pData->pipeline);
  	gst_bus_add_watch(pData->bus, bus_call, pData->loop);

	return ret;
}

int gstlinkInit_appsrc_enc_fakesink(RecordHandle *recordHandle)
{
	int ret =0;
	CustomData* pData = (CustomData* )recordHandle->context;
	if(pData == NULL)
	{
		printf("CustomData malloc failed.\n");
		return -1;
	}
	//创建空的管道
	char test_pipeline[16]={};
	sprintf(test_pipeline,"test_pipeline_%d",recordHandle->index);
	pData->pipeline = gst_pipeline_new (test_pipeline);

  	//创建元件
	g_assert(APPSRC == pData->capture_src );
	{
		pData->source = gst_element_factory_make ("appsrc", NULL);
	}

	pData->caps_src_to_convert = gst_caps_new_simple("video/x-raw",
									"format", G_TYPE_STRING, pData->format,
									"width", G_TYPE_INT, pData->width,
									"height", G_TYPE_INT, pData->height,
									"framerate", GST_TYPE_FRACTION, pData->framerate,
									1,
									 NULL);
	//printf("caps_src_simple = %s\n", gst_caps_to_string(pData->caps_src_to_convert));

	g_object_set(G_OBJECT(pData->source), "caps", pData->caps_src_to_convert, NULL);
	g_object_set(G_OBJECT(pData->source),
				"stream-type", 0,
				"is-live", TRUE,
				//"block", TRUE,
				"do-timestamp", TRUE,
				"format", GST_FORMAT_TIME, NULL);

	pData->caps_enc_to_rtp = gst_caps_new_simple("video/x-h265",
							"stream-format", G_TYPE_STRING, "byte-stream",
							"width", G_TYPE_INT, pData->width,
							"height", G_TYPE_INT, pData->height,
							"framerate", GST_TYPE_FRACTION, pData->framerate, 1,
							 NULL);

	pData->omxh265enc = gst_element_factory_make ("omxh265enc", NULL);
	pData->fakesink0  = gst_element_factory_make("fakesink", NULL);

	if (!pData->pipeline || !pData->source /*|| !pData->videoconvert0*/  )
	{
		g_printerr ("Not all elements could be created.\n");
		return -1;
    }

	gst_bin_add_many (GST_BIN(pData->pipeline), pData->source,pData->omxh265enc, pData->fakesink0,  NULL);
	if(!gst_element_link_many(pData->source, pData->omxh265enc, NULL))
	{
		g_printerr ("Elements could not be linked:data.source->data0.omxh265enc.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

    if(!gst_element_link_filtered(pData->omxh265enc, pData->fakesink0, pData->caps_enc_to_rtp))
	{
		g_printerr ("Elements could not be linked.\n");
		gst_object_unref (pData->pipeline);
		return (GstPadProbeReturn)-1;
	}

    g_object_set (pData->omxh265enc, "iframeinterval", pData->framerate, NULL);
    g_object_set (pData->omxh265enc, "bitrate", pData->bitrate, NULL);

    GstPad *h265enc_pad = gst_element_get_static_pad(pData->omxh265enc,"src");
    gst_pad_add_probe (h265enc_pad, GST_PAD_PROBE_TYPE_BUFFER,(GstPadProbeCallback) enc_buffer, pData, NULL);
    gst_object_unref(h265enc_pad);

	gst_element_sync_state_with_parent (pData->source);
    gst_element_sync_state_with_parent (pData->omxh265enc);
	gst_element_sync_state_with_parent (pData->fakesink0);

	//g_print("\n\ngst starting ...\n\n");

	/* Create gstreamer loop */
	pData->loop = g_main_loop_new(NULL, FALSE);
	pData->ret = gst_element_set_state (pData->pipeline, GST_STATE_PLAYING);
	if (pData->ret == GST_STATE_CHANGE_FAILURE)
	{
		g_printerr ("Unable to set the data.pipeline to the playing state.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

  	/* Wait until error or EOS */
	pData->bus = gst_element_get_bus(pData->pipeline);
  	gst_bus_add_watch(pData->bus, bus_call, pData->loop);

  	return ret;
}

int gstlinkInit_convert_enc_rtp(RecordHandle *recordHandle)
{
	int ret =0;

	CustomData* pData = (CustomData* )recordHandle->context;
	if(pData == NULL)
	{
		printf("CustomData malloc failed.\n");
		return -1;
	}
	//创建空的管道
	char test_pipeline[16]={};
	sprintf(test_pipeline,"test_pipeline_%d",recordHandle->index);
	pData->pipeline = gst_pipeline_new (test_pipeline);

  	//创建元件
	if(XIMAGESRC ==pData->capture_src )
	{
		pData->source = gst_element_factory_make ("ximagesrc", NULL);
		getDisplayHeight( &(pData->width), &(pData->height));
	}
	else if(APPSRC == pData->capture_src )
	{
		pData->source = gst_element_factory_make ("appsrc", NULL);
	}

	if(XIMAGESRC ==pData->capture_src )
	{
		g_object_set(pData->source, "use-damage", 0, NULL);
	}
	else if(APPSRC == pData->capture_src )
	{
		pData->caps_src_to_convert = gst_caps_new_simple("video/x-raw",
										"format", G_TYPE_STRING, pData->format,
										"width", G_TYPE_INT, pData->width,
										"height", G_TYPE_INT, pData->height,
										"framerate", GST_TYPE_FRACTION, pData->framerate*2 ,
										1,
										 NULL);
		printf("caps_src_to_convert = %s\n", gst_caps_to_string(pData->caps_src_to_convert));
		char * capsStr = g_strdup_printf("video/x-raw(memory:NVMM),width=(int)%d,height=(int)%d,alignment=(string)au,format=(string)I420,framerate=(fraction)%d/1,pixel-aspect-ratio=(fraction)1/1", pData->width, pData->height, pData->framerate);
		pData->caps_nvconv_to_enc = gst_caps_from_string(capsStr);
		g_free(capsStr);

		g_object_set(G_OBJECT(pData->source), "caps", pData->caps_src_to_convert, NULL);
		g_object_set(G_OBJECT(pData->source),
					"stream-type", 0,
					"is-live", TRUE,
					//"block", TRUE,
					"do-timestamp", TRUE,
					"format", GST_FORMAT_TIME, NULL);
	}

	pData->caps_enc_to_rtp = gst_caps_new_simple("video/x-h265",
							"stream-format", G_TYPE_STRING, "byte-stream",
							"width", G_TYPE_INT, pData->width,
							"height", G_TYPE_INT, pData->height,
							"framerate", GST_TYPE_FRACTION, pData->framerate*2, 1,
							 NULL);

	pData->videoconvert0 = gst_element_factory_make ("nvvidconv", NULL);
	pData->omxh265enc = gst_element_factory_make ("omxh265enc", NULL);
	pData->rtph265pay = gst_element_factory_make("rtph265pay", NULL);
	pData->udpsink = gst_element_factory_make("udpsink", NULL);

	if (!pData->pipeline || !pData->source || !pData->videoconvert0  )
	{
		g_printerr ("Not all elements could be created.\n");
		return -1;
    }

	gst_bin_add_many (GST_BIN(pData->pipeline), pData->source,	pData->videoconvert0, pData->omxh265enc,pData->rtph265pay, pData->udpsink,  NULL);
	if(!gst_element_link_many(pData->source,
			pData->videoconvert0, pData->omxh265enc, NULL))
	{
		g_printerr ("Elements could not be linked:data.source->data0.videoconvert0.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

    if(!gst_element_link_filtered(pData->omxh265enc, pData->rtph265pay, pData->caps_enc_to_rtp))
	{
		g_printerr ("Elements could not be linked.\n");
		gst_object_unref (pData->pipeline);
		return (GstPadProbeReturn)-1;
	}

    if(!gst_element_link_many(pData->rtph265pay, pData->udpsink, NULL))
	{
        g_printerr ("Elements could not be linked:data->rtph265pay ---> data->udpsink.\n");
		gst_object_unref (pData->pipeline);
		return (GstPadProbeReturn)-1;
	}

    g_object_set (pData->omxh265enc, "iframeinterval", pData->framerate*2, NULL);
    g_object_set (pData->omxh265enc, "bitrate", pData->bitrate, NULL);
    g_object_set (pData->rtph265pay, "config-interval", 1, NULL);
	g_object_set (pData->udpsink, "host", pData->ip_addr, NULL);
	g_object_set (pData->udpsink, "port", pData->port, NULL);

    GstPad *h265enc_pad = gst_element_get_static_pad(pData->omxh265enc,"src");
    //gst_pad_add_probe (h265enc_pad, GST_PAD_PROBE_TYPE_BUFFER,(GstPadProbeCallback) enc_buffer, pData, NULL);
    gst_object_unref(h265enc_pad);

	gst_element_sync_state_with_parent (pData->source);
	gst_element_sync_state_with_parent (pData->videoconvert0);
    gst_element_sync_state_with_parent (pData->omxh265enc);
    gst_element_sync_state_with_parent (pData->rtph265pay);
	gst_element_sync_state_with_parent (pData->udpsink);

	g_print("\n\ngst starting ...\n\n");

	/* Create gstreamer loop */
	pData->loop = g_main_loop_new(NULL, FALSE);
	pData->ret = gst_element_set_state (pData->pipeline, GST_STATE_PLAYING);
	if (pData->ret == GST_STATE_CHANGE_FAILURE)
	{
		g_printerr ("Unable to set the data.pipeline to the playing state.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

  	/* Wait until error or EOS */
	pData->bus = gst_element_get_bus(pData->pipeline);
  	gst_bus_add_watch(pData->bus, bus_call, pData->loop);

	return ret;
}

int rtp_main_init(RecordHandle *recordHandle)
{
	int ret =0;

	CustomData* pData = (CustomData* )recordHandle->context;
	if(pData == NULL)
	{
		printf("CustomData malloc failed.\n");
		return -1;
	}

  	//创建元件
	if(XIMAGESRC ==pData->capture_src )
	{
		pData->source = gst_element_factory_make ("ximagesrc", NULL);
		getDisplayHeight( &(pData->width), &(pData->height));
	}
	else if(APPSRC == pData->capture_src )
	{
		pData->source = gst_element_factory_make ("appsrc", NULL);
	}

	pData->videoconvert0 = gst_element_factory_make ("nvvidconv", NULL);
	//pData->videoconvert0 = gst_element_factory_make ("videoconvert", NULL);
//	pData->clockoverlay = gst_element_factory_make ("clockoverlay", NULL);
	pData->tee0 = gst_element_factory_make("tee", NULL);
	pData->queue0 = gst_element_factory_make("queue", NULL);
	pData->fakesink0 = gst_element_factory_make ("fakesink", NULL);
	//创建空的管道
	char test_pipeline[16]={};
	sprintf(test_pipeline,"test_pipeline_%d",recordHandle->index);
	pData->pipeline = gst_pipeline_new (test_pipeline);

	if(XIMAGESRC ==pData->capture_src )
	{
		g_object_set(pData->source, "use-damage", 0, NULL);
	}
	else if(APPSRC == pData->capture_src )
	{
		pData->caps_src_to_convert = gst_caps_new_simple("video/x-raw",
										"format", G_TYPE_STRING, pData->format,
										"width", G_TYPE_INT, pData->width,
										"height", G_TYPE_INT, pData->height,
										"framerate", GST_TYPE_FRACTION, pData->framerate , 1,
										 NULL);
		printf("caps_src_to_convert = %s\n", gst_caps_to_string(pData->caps_src_to_convert));
		char * capsStr = g_strdup_printf("video/x-raw(memory:NVMM),width=(int)%d,height=(int)%d,alignment=(string)au,format=(string)I420,framerate=(fraction)%d/1,pixel-aspect-ratio=(fraction)1/1", pData->width, pData->height, pData->framerate);
		pData->caps_nvconv_to_enc = gst_caps_from_string(capsStr);
		g_free(capsStr);

		g_object_set(G_OBJECT(pData->source), "caps", pData->caps_src_to_convert, NULL);
		g_object_set(G_OBJECT(pData->source),
					"stream-type", 0,
					"is-live", TRUE,
					//"block", TRUE,
					"do-timestamp", TRUE,
					"format", GST_FORMAT_TIME, NULL);
	}

	pData->caps_enc_to_rtp = gst_caps_new_simple("video/x-h265",
							"stream-format", G_TYPE_STRING, "byte-stream",
							"width", G_TYPE_INT, pData->width,
							"height", G_TYPE_INT, pData->height,
							"framerate", GST_TYPE_FRACTION, pData->framerate, 1,
							 NULL);
//|| !pData->clockoverlay
  	if (!pData->pipeline || !pData->source || !pData->videoconvert0 || !pData->tee0 || !pData->queue0 || !pData->fakesink0 )
	{
		g_printerr ("Not all elements could be created.\n");
		return -1;
    }
//,pData->clockoverlay
	gst_bin_add_many (GST_BIN(pData->pipeline), pData->source,pData->videoconvert0, pData->tee0, pData->queue0, pData->fakesink0, NULL);

	if(!gst_element_link_many(pData->source, pData->videoconvert0, pData->tee0, NULL))
	{
		g_printerr ("Elements could not be linked:data.source->data0.videoconvert0.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

	if(!gst_element_link_many(pData->queue0, pData->fakesink0, NULL))
	{
		g_printerr ("Elements could not be linked:pData->source   pData->videoconvert0.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

	//Manually link the tee0, which has "Request" pads
	pData->tee0_src_pad_template = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (pData->tee0), "src_%u");

	pData->tee0queue0_srcpad = gst_element_request_pad (pData->tee0, pData->tee0_src_pad_template, NULL, NULL);
	pData->tee0queue1_srcpad = gst_element_request_pad (pData->tee0, pData->tee0_src_pad_template, NULL, NULL);

	GstPad *queue0filesink0_sinkpad = gst_element_get_static_pad (pData->queue0, "sink");

	if (gst_pad_link (pData->tee0queue0_srcpad, queue0filesink0_sinkpad) != GST_PAD_LINK_OK) {
		g_printerr ("Tee0 could not be linked to data.queue0filesink0.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}
	gst_object_unref (queue0filesink0_sinkpad);

	gstCaptureEnable(recordHandle, 1);

	g_print("\n\ngst starting ...\n\n");

	/* Create gstreamer loop */
	pData->loop = g_main_loop_new(NULL, FALSE);
	pData->ret = gst_element_set_state (pData->pipeline, GST_STATE_PLAYING);
	if (pData->ret == GST_STATE_CHANGE_FAILURE)
	{
		g_printerr ("Unable to set the data.pipeline to the playing state.\n");
		gst_object_unref (pData->pipeline);
		return -1;
	}

  	/* Wait until error or EOS */
	pData->bus = gst_element_get_bus(pData->pipeline);
  	gst_bus_add_watch(pData->bus, bus_call, pData->loop);

  	return 0;
}


void * rtp_main_loop(void* arg)
{
	RecordHandle * recordHandle = (RecordHandle *)arg;
	CustomData* pCustomData = (CustomData *)recordHandle->context;
	g_main_loop_run(pCustomData->loop);  //链路停止，释放资源�?
	printf("rtp_main_loop done.\n");
	return NULL;
}


int gstCaptureEnable(RecordHandle *recordHandle, unsigned short bEnable)
{
	if(recordHandle == NULL)
	{
		g_print("recordHandle == NULL\n");
		return -1;
	}
	CustomData* pData = (CustomData *)recordHandle->context;
	if(bEnable)
	{
		gst_pad_add_probe (pData->tee0queue1_srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, enc_tick_cb, pData, (GDestroyNotify)NULL);
	}
	else
	{
		gst_pad_add_probe (pData->tee0queue1_srcpad, GST_PAD_PROBE_TYPE_IDLE, enc_unlink_cb, pData, (GDestroyNotify)NULL);
	}

	return 0;
}
/*int gstCapturePushData(RecordHandle *recordHandle, char *pbuffer , int datasize)
{
	if(recordHandle == NULL)
	{
		return -1;
	}
	CustomData* pData = (CustomData *)recordHandle->context;

	if(pData==NULL || pData->source == NULL)
	{
		return -1;
	}
	GstMapInfo info;
	GstBuffer *buffer;
	guint size;
	GstFlowReturn ret;

	{
		buffer = gst_buffer_new_allocate(NULL, datasize, NULL);
		if(gst_buffer_map(buffer, &info, GST_MAP_WRITE))
		{
			memcpy(info.data, pbuffer, datasize);
			GST_BUFFER_PTS(buffer) = pData->buffer_timestamp;
			GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, pData->framerate);
			pData->buffer_timestamp+=GST_BUFFER_DURATION(buffer);
			gst_buffer_unmap(buffer, &info);
		}
		gst_buffer_ref(buffer);
		ret = gst_app_src_push_buffer(GST_APP_SRC(pData->source), buffer);
		if( ret != GST_FLOW_OK )
		{
			g_print("error: \n");
		}
		gst_buffer_unref(buffer);
	}

	return 0;
}*/

static void * thrdhndl_push_buffer_scheduler(void* arg)
{
	CustomData* pData = (CustomData *)arg;
	struct timeval timerStart;
	unsigned long ulCount = 0;
	gettimeofday(&timerStart, NULL);
	pData->bPush = true;
	while(pData->bPush){
		OSA_BufInfo* bufInfo = image_queue_getFull(&pData->pushBuffQueue);
		if(bufInfo != NULL){
			GstMapInfo *info = (GstMapInfo *)bufInfo->resource;
			GstBuffer *buffer = (GstBuffer *)bufInfo->physAddr;
			OSA_assert(buffer != NULL);

			GST_BUFFER_PTS(buffer) = pData->buffer_timestamp;
			GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, pData->framerate);
			pData->buffer_timestamp+=GST_BUFFER_DURATION(buffer);

			gst_buffer_unmap(buffer, info);
			gst_buffer_ref(buffer);
			GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(pData->source), buffer);
			if( ret != GST_FLOW_OK )
			{
				g_print("error: \n");
			}
			gst_buffer_unref(buffer);
			buffer = gst_buffer_new_allocate(NULL, bufInfo->size, NULL);
			int iret = gst_buffer_map(buffer, info, GST_MAP_WRITE);
			OSA_assert(iret != 0);
			bufInfo->virtAddr = info->data;
			bufInfo->physAddr = buffer;
			bufInfo->resource = info;

			image_queue_putEmpty(&pData->pushBuffQueue, bufInfo);
		}
		ulCount ++;
		bool bSync = false;
	    struct timeval timerStop, timerElapsed;
	    gettimeofday(&timerStop, NULL);
	    timersub(&timerStop, &timerStart, &timerElapsed);
	    double ms = (timerElapsed.tv_sec*1000.0+timerElapsed.tv_usec/1000.0);
	    double nms = 1000.0/pData->framerate*ulCount;
	    double wms = nms-ms-0.5;
	    if(wms<0.000001 || wms>1000.0/pData->framerate+5.0){
	    	bSync = true;
	    	g_print("%s %d: tm sync!! \n", __func__, __LINE__);
	    }
	    if(image_queue_fullCount(&pData->pushBuffQueue)>1){
	    	bufInfo = image_queue_getFull(&pData->pushBuffQueue);
	    	image_queue_putEmpty(&pData->pushBuffQueue, bufInfo);
	    	bSync = true;
	    	g_print("%s %d: full sync!! \n", __func__, __LINE__);
	    }
	    if(bSync){
	    	gettimeofday(&timerStart, NULL);
	    	ulCount = 0;
	    }else{
			struct timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = wms*1000.0;
			select( 0, NULL, NULL, NULL, &timeout );
	    }
	}
	return NULL;
}

static void * thrdhndl_push_buffer(void* arg)
{
	CustomData* pData = (CustomData *)arg;
	pData->bPush = true;
	while(pData->bPush){
		OSA_semWait(&pData->pushSem, OSA_TIMEOUT_FOREVER);
		if(!pData->bPush)
			break;
		OSA_BufInfo* bufInfo = image_queue_getFull(&pData->pushBuffQueue);
		if(bufInfo != NULL){
			GstMapInfo *info = (GstMapInfo *)bufInfo->resource;
			GstBuffer *buffer = (GstBuffer *)bufInfo->physAddr;
			OSA_assert(buffer != NULL);

			GST_BUFFER_PTS(buffer) = pData->buffer_timestamp;
			GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, pData->framerate);
			pData->buffer_timestamp+=GST_BUFFER_DURATION(buffer);

			gst_buffer_unmap(buffer, info);
			gst_buffer_ref(buffer);
			GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(pData->source), buffer);
			if( ret != GST_FLOW_OK )
			{
				g_print("error: \n");
			}
			gst_buffer_unref(buffer);
			buffer = gst_buffer_new_allocate(NULL, bufInfo->size, NULL);
			int iret = gst_buffer_map(buffer, info, GST_MAP_WRITE);
			OSA_assert(iret != 0);
			bufInfo->virtAddr = info->data;
			bufInfo->physAddr = buffer;
			bufInfo->resource = info;

			image_queue_putEmpty(&pData->pushBuffQueue, bufInfo);
		}
	    if(image_queue_fullCount(&pData->pushBuffQueue)>0){
	    	bufInfo = image_queue_getFull(&pData->pushBuffQueue);
	    	image_queue_putEmpty(&pData->pushBuffQueue, bufInfo);
	    	g_print("%s %d: full !! \n", __func__, __LINE__);
	    }
	}
	return NULL;
}
int gstCapturePushData(RecordHandle *recordHandle, char *pbuffer , int datasize)
{
	if(recordHandle == NULL)
	{
		return -1;
	}
	CustomData* pData = (CustomData *)recordHandle->context;

	if(pData==NULL || pData->source == NULL || APPSRC != pData->capture_src)
	{
		return -1;
	}

	OSA_BufInfo* bufInfo = image_queue_getEmpty(&pData->pushBuffQueue);
	if(bufInfo != NULL)
	{
		//GstMapInfo *info = (GstMapInfo *)bufInfo->resource;
		//GstBuffer *buffer = (GstBuffer *)bufInfo->physAddr;
		memcpy(bufInfo->virtAddr, pbuffer, datasize);
		image_queue_putFull(&pData->pushBuffQueue, bufInfo);
		OSA_semSignal(&pData->pushSem);
	}

	return 0;
}

RecordHandle * gstCaptureInit( GstCapture_data gstCapture_data )
{
	int res;
	static int createNum = 0;
	pthread_t thread_0;
	printf("\r\n gst_Capture--------------Build date: %s %s \r\n", __DATE__, __TIME__);
	RecordHandle * recordHandle = (RecordHandle *)malloc(sizeof(RecordHandle));
	memset(recordHandle, 0, sizeof(RecordHandle));
	recordHandle->index = createNum;
	recordHandle->width = gstCapture_data.width;
	recordHandle->height = gstCapture_data.height;
	recordHandle->ip_port = gstCapture_data.ip_port;
	recordHandle->filp_method = gstCapture_data.filp_method;
	recordHandle->capture_src = gstCapture_data.capture_src;
	recordHandle->framerate = gstCapture_data.framerate;
	recordHandle->bitrate = gstCapture_data.bitrate;
	recordHandle->sd_cb=gstCapture_data.sd_cb;
	for(int i=0;i<ENC_QP_PARAMS_COUNT;i++)
		recordHandle->Q_PIB[i]=gstCapture_data.Q_PIB[i];
	OSA_assert(gstCapture_data.format!=NULL);
	strcpy(recordHandle->format, gstCapture_data.format);
	if(gstCapture_data.ip_addr!=NULL)
		strcpy(recordHandle->ip_addr, gstCapture_data.ip_addr);
	recordHandle->bEnable = FALSE;

	recordHandle->context = (CustomData *)malloc(sizeof(CustomData));
	CustomData* pData = (CustomData* )recordHandle->context;
	memset(pData, 0, sizeof(CustomData));
	pData->record = recordHandle;
	pData->height = recordHandle->height;
	pData->width = recordHandle->width;
	pData->framerate = recordHandle->framerate;
	pData->bitrate = recordHandle->bitrate;
	pData->filp_method = recordHandle->filp_method;
	pData->capture_src = recordHandle->capture_src;
	strcpy(pData->format, recordHandle->format);
	strcpy(pData->ip_addr, recordHandle->ip_addr);
	pData->notify = gstCapture_data.notify;
	pData->port = recordHandle->ip_port;
	pData->queue1 = NULL;
	pData->omxh265enc = NULL;
	pData->nvvidconv0 = NULL;
	pData->fakesink1 = NULL;
	/* Initialize GStreamer */
	static bool bGstInit = false;
	if(!bGstInit)
		gst_init (NULL, NULL);
	bGstInit = true;

	if(APPSRC == gstCapture_data.capture_src){
		res = image_queue_create(&pData->pushBuffQueue, 3, pData->width*pData->height*3, memtype_null);
		for(int i=0; i<3; i++){
			GstMapInfo *info = new GstMapInfo;
			GstBuffer *buffer;
			buffer = gst_buffer_new_allocate(NULL, pData->pushBuffQueue.bufInfo[i].size, NULL);
			int iret = gst_buffer_map(buffer, info, GST_MAP_WRITE);
			OSA_assert(iret != 0);
			pData->pushBuffQueue.bufInfo[i].virtAddr = info->data;
			pData->pushBuffQueue.bufInfo[i].physAddr = buffer;
			pData->pushBuffQueue.bufInfo[i].resource = info;
		}
		res = OSA_semCreate(&pData->pushSem, 1, 0);
		pthread_create(&pData->threadPushBuffer, NULL, thrdhndl_push_buffer, (void*)pData);
		OSA_assert(pData->threadPushBuffer != 0);
		recordHandle->pushQueue = &pData->pushBuffQueue;
		recordHandle->pushSem = &pData->pushSem;
	}

	res = image_queue_create(&pData->outBuffQueue, 3, pData->width*pData->height*3, memtype_null);
	if(pData->notify == NULL){
		pData->outSem = new OSA_SemHndl;
		res = OSA_semCreate(pData->outSem, 1, 0);
	}else{
		pData->outSem = (OSA_SemHndl *)pData->notify;
	}

	//res = rtp_main_init(recordHandle);  //初始化gstreamer.
	if(gstCapture_data.ip_addr!=NULL)
		res = gstlinkInit_convert_enc_rtp(recordHandle);
	else
	{
		if(APPSRC == gstCapture_data.capture_src && strcmp(recordHandle->format, "I420") == 0)
		{
			res = gstlinkInit_appsrc_enc_fakesink(recordHandle);
		}
		else{
			res = gstlinkInit_convert_enc_fakesink(recordHandle);
		}
	}

	if(res == -1)
	{
		g_printerr("gst record init failed\n");
		return NULL;
	}

	//( (CustomData *)(recordHandle->context))->hdlSink = demoInterfacesCreate(NULL, gstCapture_data.ip_port);

	//res = pthread_create(&thread_0, NULL, rtp_main_loop, (void*)recordHandle);
	//if(res == -1)
	//	return NULL;

	createNum ++;

	return recordHandle;
}

int gstCaptureUninit(RecordHandle *recordHandle)
{
	if(recordHandle != NULL)
	{
		if(recordHandle->context != NULL)
		{
			CustomData* pData = (CustomData *)recordHandle->context;
			if(pData->threadPushBuffer != 0){
				void *returnVal;
				pData->bPush = false;
				OSA_semSignal(&pData->pushSem);
				pthread_cancel(pData->threadPushBuffer);
				pthread_join(pData->threadPushBuffer, &returnVal);
				OSA_semDelete(&pData->pushSem);
			}
			if(pData->threadTimer != 0){
				void *returnVal;
				pData->bOut = false;
				pthread_cancel(pData->threadTimer);
				pthread_join(pData->threadTimer, &returnVal);
			}
			if(pData->threadPushBuffer != 0){
				void *returnVal;
				pData->bOut = false;
				OSA_semSignal(pData->outSem);
				pthread_cancel(pData->threadOutBuffer);
				pthread_join(pData->threadOutBuffer, &returnVal);
				if(pData->outSem != (OSA_SemHndl *)pData->notify){
					OSA_semDelete(pData->outSem);
					delete pData->outSem;
				}
			}
			for(int i=0; i<3; i++){
				GstMapInfo *info;
				GstBuffer *buffer;
				buffer = (GstBuffer *)pData->pushBuffQueue.bufInfo[i].physAddr;
				info = (GstMapInfo *)pData->pushBuffQueue.bufInfo[i].resource;
				gst_buffer_unmap(buffer, info);
				gst_buffer_unref(buffer);
				delete info;
			}
			image_queue_delete(&pData->pushBuffQueue);
			image_queue_delete(&pData->outBuffQueue);
			free(pData);
			recordHandle->context = NULL;
		}
		free(recordHandle);

		recordHandle = NULL;
	}

	return 0;
}

int ChangeBitRate(RecordHandle *recordHandle,unsigned int change_data)
{
    gint bitrate = 0;
    if(recordHandle == NULL)
	{
		return -1;
	}
    	CustomData* pData = (CustomData *)recordHandle->context;
    if(pData->pipeline == NULL || pData->omxh265enc == NULL)
    {
        printf("CustomData need wait init element end.\n");
        return -1;
    }
    gst_element_set_state(pData->pipeline, GST_STATE_PAUSED);
	g_object_set (pData->omxh265enc, "bitrate",  change_data,  NULL);
	g_object_get (pData->omxh265enc, "bitrate", &bitrate, NULL);
    gst_element_set_state(pData->pipeline, GST_STATE_PLAYING);
    pData->bitrate=bitrate;
    recordHandle->bitrate=bitrate;
    return bitrate;
}

int ChangeQP_range(RecordHandle *recordHandle,int minQP, int maxQP, int minQI, int maxQI, int minQB, int maxQB)
{
	   if(recordHandle == NULL)
		{
			return -1;
		}
	    CustomData* pData = (CustomData *)recordHandle->context;
	if(pData == NULL)
	{
		printf("CustomData malloc failed.\n");
		return -1;
	}
	if(pData->pipeline == NULL || pData->omxh265enc == NULL)
	{
		printf("CustomData need wait init element end.\n");
		return -1;
	}

	gst_element_set_state(pData->pipeline, GST_STATE_PAUSED);
	gst_element_set_state(pData->pipeline, GST_STATE_NULL);
	char str[100];
	sprintf(str, "%d,%d:%d,%d:%d,%d", minQP, maxQP, minQI, maxQI, -1, -1);
	printf("%s set: %s \n", __func__, str);
	g_object_set (pData->omxh265enc, "qp-range",  str,  NULL);
	recordHandle->Q_PIB[ENC_MIN_QP]=minQP;
	recordHandle->Q_PIB[ENC_MAX_QP]=maxQP;
	recordHandle->Q_PIB[ENC_MIN_QI]=minQI;
	recordHandle->Q_PIB[ENC_MAX_QI]=maxQI;
	recordHandle->Q_PIB[ENC_MIN_QB]=minQB;
	recordHandle->Q_PIB[ENC_MAX_QB]=maxQB;

	gst_element_set_state(pData->pipeline, GST_STATE_PLAYING);
	return 0;
}
