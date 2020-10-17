/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ALSACapture.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** ALSA capture overide of V4l2Capture
**                                                                                    
** -------------------------------------------------------------------------*/

#ifdef HAVE_ALSA

#include "ALSACapture.h"


ALSACapture* ALSACapture::createNew(const ALSACaptureParameters & params) 
{ 
	ALSACapture* capture = new ALSACapture(params);
	if (capture) 
	{
		if (capture->getFd() == -1) 
		{
			delete capture;
			capture = NULL;
		}
	}
	return capture; 
}

ALSACapture::~ALSACapture()
{
	this->close();
}

void ALSACapture::close()
{
	if (m_pcm != NULL)
	{
		snd_pcm_close (m_pcm);
		m_pcm = NULL;
	}
}

ALSACapture::ALSACapture(const ALSACaptureParameters & params) : m_pcm(NULL), m_bufferSize(0), m_periodSize(0), m_params(params)
{
	LOG(NOTICE) << "Open ALSA device: \"" << params.m_devName << "\"";
	
	snd_pcm_hw_params_t *hw_params = NULL;
	snd_pcm_uframes_t period_frames = 960;

	int err = 0;
	
	if( m_params.m_useOpus ) {
		LOG(NOTICE) << "Using Opus encoding for audio";
		useOpus = true;
	}
	else {
		useOpus = false;
	}

	// open PCM device
	if ((err = snd_pcm_open (&m_pcm, m_params.m_devName.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		LOG(ERROR) << "cannot open audio device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
	}
				
	// configure hw_params
	else if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
		LOG(ERROR) << "cannot allocate hardware parameter structure device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	else if ((err = snd_pcm_hw_params_any (m_pcm, hw_params)) < 0) {
		LOG(ERROR) << "cannot initialize hardware parameter structure device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}			
	else if ((err = snd_pcm_hw_params_set_access (m_pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		LOG(ERROR) << "cannot set access type device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	else if (this->configureFormat(hw_params) < 0) {
		this->close();
	}
	else if ((err = snd_pcm_hw_params_set_rate_near (m_pcm, hw_params, &m_params.m_sampleRate, 0)) < 0) {
		LOG(ERROR) << "cannot set sample rate device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	else if ((err = snd_pcm_hw_params_set_channels (m_pcm, hw_params, m_params.m_channels)) < 0) {
		LOG(ERROR) << "cannot set channel count device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	else if ((err = snd_pcm_hw_params (m_pcm, hw_params)) < 0) {
		LOG(ERROR) << "cannot set parameters device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}

	// Set period size
	// else if ((err = snd_pcm_hw_params_set_period_size_near (m_pcm, hw_params, &period_frames, 0)) < 0) {
	// 	LOG(ERROR) << "Error in snd_pcm_hw_params_set_period_size_near: " << m_params.m_devName << " error:" <<  snd_strerror (err);
	// 	this->close();
	// }

	
	// get buffer size
	else if ((err = snd_pcm_get_params(m_pcm, &m_bufferSize, &m_periodSize)) < 0) {
		LOG(ERROR) << "cannot get parameters device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	
	// start capture
	else if ((err = snd_pcm_prepare (m_pcm)) < 0) {
		LOG(ERROR) << "cannot prepare audio interface for use device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}			
	else if ((err = snd_pcm_start (m_pcm)) < 0) {
		LOG(ERROR) << "cannot start audio interface for use device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}			
	
	LOG(NOTICE) << "ALSA device: \"" << m_params.m_devName << "\" buffer_size:" << m_bufferSize << " period_size:" << m_periodSize << " rate:" << m_params.m_sampleRate;

	PCMInfo(hw_params);

	if (useOpus) {

		// Initialize the Opus Encoder
		LOG(NOTICE) << "Creating Opus encoder";

		encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &err);
		if (err < 0 ) {
			LOG(ERROR) << "Failed to create Opus encoder: " << opus_strerror(err);
		}

		/* Set the desired bit-rate. You can also set other parameters if needed.
		  The Opus library is designed to have good defaults, so only set
		  parameters you know you need. Doing otherwise is likely to result
		  in worse quality, but better. */

		opus_encoder_ctl(encoder, OPUS_SET_BITRATE(48000));
		opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(0));

	}

}



void ALSACapture::PCMInfo(snd_pcm_hw_params_t *hw_params) {
	// Display information about the PCM interface
	// https://www.linuxjournal.com/article/6735?page=0,1

	snd_pcm_t *handle = m_pcm;
	snd_pcm_hw_params_t *params = hw_params;
	snd_pcm_uframes_t frames;

	snd_pcm_format_t pcm_format;
	snd_pcm_subformat_t snd_pcm_subformat;


	int rc;
	unsigned int val, val2;
	int dir;


	printf("\n--- PCM Information ---\n\n");
	printf("PCM handle name = '%s'\n", snd_pcm_name(handle));

	printf("PCM state = %s\n", snd_pcm_state_name(snd_pcm_state(handle)));

	snd_pcm_hw_params_get_access(params, (snd_pcm_access_t *) &val);
	printf("access type = %s\n", snd_pcm_access_name((snd_pcm_access_t)val));

	snd_pcm_hw_params_get_format(params, &pcm_format);
	printf("format = '%s' (%s)\n",

	snd_pcm_format_name(pcm_format),
	snd_pcm_format_description(pcm_format));

	snd_pcm_hw_params_get_subformat(params, &snd_pcm_subformat);

	printf("subformat = '%s' (%s)\n",
	snd_pcm_subformat_name(snd_pcm_subformat),
	snd_pcm_subformat_description(snd_pcm_subformat));

	snd_pcm_hw_params_get_channels(params, &val);
	printf("channels = %d\n", val);

	snd_pcm_hw_params_get_rate(params, &val, &dir);
	printf("rate = %d bps\n", val);

	snd_pcm_hw_params_get_period_time(params,
	                                &val, &dir);
	printf("period time = %d us\n", val);

	snd_pcm_hw_params_get_period_size(params,
	                                &frames, &dir);
	printf("period size = %d frames\n", (int)frames);

	snd_pcm_hw_params_get_buffer_time(params,
	                                &val, &dir);
	printf("buffer time = %d us\n", val);

	snd_pcm_hw_params_get_buffer_size(params,
	                     (snd_pcm_uframes_t *) &val);
	printf("buffer size = %d frames\n", val);

	snd_pcm_hw_params_get_periods(params, &val, &dir);
	printf("periods per buffer = %d frames\n", val);

	snd_pcm_hw_params_get_rate_numden(params,
	                                &val, &val2);
	printf("exact rate = %d/%d bps\n", val, val2);

	val = snd_pcm_hw_params_get_sbits(params);
	printf("significant bits = %d\n", val);

	snd_pcm_hw_params_get_tick_time(params,
	                              &val, &dir);
	printf("tick time = %d us\n", val);

	val = snd_pcm_hw_params_is_batch(params);
	printf("is batch = %d\n", val);

	val = snd_pcm_hw_params_is_block_transfer(params);
	printf("is block transfer = %d\n", val);

	val = snd_pcm_hw_params_is_double(params);
	printf("is double = %d\n", val);

	val = snd_pcm_hw_params_is_half_duplex(params);
	printf("is half duplex = %d\n", val);

	val = snd_pcm_hw_params_is_joint_duplex(params);
	printf("is joint duplex = %d\n", val);

	val = snd_pcm_hw_params_can_overrange(params);
	printf("can overrange = %d\n", val);

	val = snd_pcm_hw_params_can_mmap_sample_resolution(params);
	printf("can mmap = %d\n", val);

	val = snd_pcm_hw_params_can_pause(params);
	printf("can pause = %d\n", val);

	val = snd_pcm_hw_params_can_resume(params);
	printf("can resume = %d\n", val);

	val = snd_pcm_hw_params_can_sync_start(params);
	printf("can sync start = %d\n", val);

}


int ALSACapture::configureFormat(snd_pcm_hw_params_t *hw_params) {
	
	// try to set format, widht, height
	std::list<snd_pcm_format_t>::iterator it;
	for (it = m_params.m_formatList.begin(); it != m_params.m_formatList.end(); ++it) {
		snd_pcm_format_t format = *it;
		int err = snd_pcm_hw_params_set_format (m_pcm, hw_params, format);
		if (err < 0) {
			LOG(NOTICE) << "cannot set sample format device: " << m_params.m_devName << " to:" << format << " error:" <<  snd_strerror (err);
		} else {
			LOG(NOTICE) << "set sample format device: " << m_params.m_devName << " to:" << format << " ok";
			m_fmt = format;
			return 0;
		}		
	}
	return -1;
}


size_t ALSACapture::read(char* buffer, size_t bufferSize)
{

	// How often is this actually called?
	// My best guess is it is called for every frame (i.e. 25FPS)

	// In one call I fetch period time = 21333 us of audio
	// m_periodSize by default seems to be 1024


    int num_samples = bufferSize / sizeof(short);
    short localBuffer[num_samples];
    int bytesRead = 0;

	size_t size = 0;
	int fmt_phys_width_bytes = 0;

	if (m_pcm != 0)
	{
		// bits per sample
		int fmt_phys_width_bits = snd_pcm_format_physical_width(m_fmt);

		// Bytes per sample (2 bytes)
		fmt_phys_width_bytes = fmt_phys_width_bits / 8;

		// Reading m_periodSize = 1024  * 16 bits
		snd_pcm_sframes_t ret = snd_pcm_readi (m_pcm, buffer, m_periodSize*fmt_phys_width_bytes);

		LOG(DEBUG) << "ALSA buffer in_size:" << m_periodSize*fmt_phys_width_bytes << " read_size:" << ret;
		if (ret > 0) {
			size = ret;				
			
			// swap if capture in not in network order
			if (!snd_pcm_format_big_endian(m_fmt)) {
				LOG(DEBUG) << "Swapping buffer because bytes are not in network order";

				for(unsigned int i = 0; i < size; i++){
					char * ptr = &buffer[i * fmt_phys_width_bytes * m_params.m_channels];
					
					for(unsigned int j = 0; j < m_params.m_channels; j++){
						ptr += j * fmt_phys_width_bytes;
						for (int k = 0; k < fmt_phys_width_bytes/2; k++) {
							char byte = ptr[k];
							ptr[k] = ptr[fmt_phys_width_bytes - 1 - k];
							ptr[fmt_phys_width_bytes - 1 - k] = byte; 
						}
					}
				}
			}
		}
	}

	bytesRead = size * m_params.m_channels * fmt_phys_width_bytes;



	if (useOpus) {

		// Copy contents of buffer to localBuffer
		memcpy(&localBuffer, buffer, bufferSize);

		// Encode the frame
		bytesRead = opus_encode(encoder, localBuffer, 960, (unsigned char *)buffer, bufferSize);

		if (bytesRead < 0) {
		    LOG(ERROR) << "Error encoding Opus frame: " << opus_strerror(bytesRead);
		    return -1;
		}
	}


	LOG(DEBUG) << "Audio bytesRead: " << bytesRead;

	return bytesRead;
}
		
int ALSACapture::getFd()
{
	unsigned int nbfs = 1;
	struct pollfd pfds[nbfs]; 
	pfds[0].fd = -1;
	
	if (m_pcm != 0)
	{
		int count = snd_pcm_poll_descriptors_count (m_pcm);
		int err = snd_pcm_poll_descriptors(m_pcm, pfds, count);
		if (err < 0) {
			fprintf (stderr, "cannot snd_pcm_poll_descriptors (%s)\n", snd_strerror (err));
		}
	}
	return pfds[0].fd;
}
		
#endif


