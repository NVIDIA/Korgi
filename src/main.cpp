/*
* Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include <stdio.h>
#include <thread>

#ifdef _WIN32
#include <WinSock2.h>
#include <mmsystem.h>
#include <WS2tcpip.h>
#else
#include <alsa/asoundlib.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <unordered_map>
#include <algorithm>
#include <signal.h>

#ifdef _WIN32
#pragma comment(lib, "winmm")
#pragma comment(lib, "ws2_32")
#endif

using namespace std;

#ifdef _WIN32
WSADATA g_wsaData = {};
SOCKET g_SendSocket = 0;
sockaddr_in g_sendToAddr = {};
HMIDIIN g_midiInHandle = {};
#else
int g_SendSocket = 0;
struct sockaddr_in g_sendToAddr = {};
snd_seq_t *g_midiInHandle = NULL;
snd_seq_port_subscribe_t *g_midiSubscription = NULL;
int g_midiPort = 0;
struct pollfd *g_pollFds = NULL;
int g_pollFdCount = 0;

// Function shims
#define InetPton inet_pton
#define strcpy_s strcpy
#define sprintf_s sprintf
#endif

#include "control_surface_map.h"

bool g_terminate = false;

struct KnobMapping
{
	std::string name;
	float min_value;
	float max_value;
};

struct KorgiConfig
{
	string address = "127.0.0.1";
	int port = 27910;
	string password;
	int device = 0;
	string device_name = "nanoKONTROL2";
	unordered_map<int, string> buttons;
	unordered_map<int, KnobMapping> knobs;
} g_config;

bool OpenSocket()
{
#ifdef _WIN32
	//Startup WinSock
	if (0 != WSAStartup(MAKEWORD(2, 2), &g_wsaData))
	{
		fprintf(stderr, "error: failed to initialize WinSock\n");
		return false;
	}
#endif

	// Setup broadcast socket
	g_SendSocket = socket(AF_INET, SOCK_DGRAM, 0);
	g_sendToAddr.sin_port = htons(g_config.port);
	g_sendToAddr.sin_family = AF_INET;
	if (0 == InetPton(AF_INET, g_config.address.c_str(), (void*)&g_sendToAddr.sin_addr.s_addr))
	{
		fprintf(stderr, "error: failed to translate the target IP address\n");
		return false;
	}

	printf("korgi: connected to %s:%d\n", g_config.address.c_str(), g_config.port);

	return true;
}

void CloseSocket()
{
#ifdef _WIN32
	closesocket(g_SendSocket);
	g_SendSocket = 0;
	WSACleanup();
#else
	close(g_SendSocket);
	g_SendSocket = 0;
#endif
}

void HandleMidiInput(unsigned char midiChannel, unsigned char midiValue)
{
	static char previousChannel = -1;
	if (previousChannel == midiChannel)
		printf("\r");
	else
		printf("\n");
	previousChannel = midiChannel;

	char command[256];
	command[0] = 0;

	const auto& button = g_config.buttons.find(midiChannel);
	const auto& knob = g_config.knobs.find(midiChannel);

	if (button != g_config.buttons.end())
	{
		if (midiValue > 0)
		{
			strcpy_s(command, button->second.c_str());

			printf("korgi: button %u \"%s\"", midiChannel, button->second.c_str());
		}
	}
	else if (knob != g_config.knobs.end())
	{
		float fvalue = (float)midiValue / 127.f;
		fvalue = max(0.f, min(1.f, fvalue));

		float variable_value = knob->second.min_value * (1.f - fvalue) + knob->second.max_value * fvalue;
		sprintf_s(command, "%s %.3f", knob->second.name.c_str(), variable_value);

		printf("korgi: knob %u \"%s\"   ", midiChannel, command);
	}
	else
	{ 
		printf("korgi: channel %u unmapped value %d   ", midiChannel, midiValue);
	}

	if (*command)
	{
		char udp_message[256];
		sprintf_s(udp_message, "\xff\xff\xff\xffrcon %s %s", g_config.password.c_str(), command);

		sendto(g_SendSocket, udp_message, int(strlen(udp_message)) + 1, 0, (sockaddr*)&g_sendToAddr, sizeof(g_sendToAddr));
	}

	// Don't want to buffer output since we want concolse output to match what's
	// going across UDP pipe in terms of update-parity
	fflush(0);
}

#ifdef _WIN32
void CALLBACK MidiInCallback(HMIDIIN  hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (wMsg != MIM_DATA)
		return;

	char midiChannel = (dwParam1 >> 8) & 0xff;
	char midiValue = (dwParam1 >> 16) & 0xff;

	HandleMidiInput(midiChannel, midiValue);
}
#endif

#ifdef __linux__
unsigned char GetMidiDeviceMatchingName(const char *deviceName)
{
	int err;
	snd_seq_client_info_t *client;
	snd_seq_client_info_alloca(&client);

	err = snd_seq_get_any_client_info(g_midiInHandle, 0, client);
	while (!err)
	{
		const char *clientName = snd_seq_client_info_get_name(client);
		if (!strcmp(clientName, deviceName))
		{
			return snd_seq_client_info_get_client(client);
		}

		err = snd_seq_query_next_client(g_midiInHandle, client);
	}

	return 0;
}
#endif

bool OpenMidiDevice()
{
#ifdef _WIN32
	if (midiInOpen(&g_midiInHandle, g_config.device, (DWORD_PTR)MidiInCallback, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
	{
		fprintf(stderr, "error: failed to open the midi device\n");
		return false;
	}

	midiInStart(g_midiInHandle);

	MIDIINCAPS inCaps = {};
	if (midiInGetDevCaps((UINT_PTR)&g_config.device, &inCaps, sizeof(MIDIINCAPS)) == MMSYSERR_NOERROR)
		printf("korgi: opened midi device %d called \"%s\"\n", g_config.device, inCaps.szPname);
	else
		printf("korgi: opened midi device %d but couldn't get its name...\n", g_config.device);

	return true;
#else
	if (snd_seq_open(&g_midiInHandle, "default", SND_SEQ_OPEN_INPUT, 0))
	{
		fprintf(stderr, "error: failed to connect to ALSA\n");
		return false;
	}

	g_midiPort = snd_seq_create_simple_port(g_midiInHandle,
											"Korgi - MIDI to RCON",
											SND_SEQ_PORT_CAP_WRITE,
											SND_SEQ_PORT_TYPE_MIDI_GENERIC);

	unsigned char korgDeviceId = GetMidiDeviceMatchingName(g_config.device_name.c_str());
	if (!korgDeviceId)
	{
		fprintf(stderr, "error: failed to detect midi device '%s'\n", g_config.device_name.c_str());
		return false;
	}

	snd_seq_addr_t korgDevice, korgiListener;

	korgDevice.client = korgDeviceId;
	korgDevice.port = 0;
	korgiListener.client = snd_seq_client_id(g_midiInHandle);
	korgiListener.port = 0;

	if (snd_seq_port_subscribe_malloc(&g_midiSubscription))
	{
		fprintf(stderr, "error: failed to allocate ALSA subscription, out of memory?\n");
		return false;
	}

	snd_seq_port_subscribe_set_sender(g_midiSubscription, &korgDevice);
	snd_seq_port_subscribe_set_dest(g_midiSubscription, &korgiListener);
	snd_seq_port_subscribe_set_queue(g_midiSubscription, 1);
	snd_seq_port_subscribe_set_time_update(g_midiSubscription, 1);
	snd_seq_port_subscribe_set_time_real(g_midiSubscription, 1);

	if(snd_seq_subscribe_port(g_midiInHandle, g_midiSubscription))
	{
		fprintf(stderr, "error: failed to connect to midi device\n");
		return false;
	}

	g_pollFdCount = snd_seq_poll_descriptors_count(g_midiInHandle, POLLIN);
	g_pollFds = (struct pollfd *)calloc(g_pollFdCount, sizeof(struct pollfd));

	if (!g_pollFds)
	{
		fprintf(stderr, "error: failed to allocate poll fds, out of memory?\n");
		return false;
	}

	if (snd_seq_poll_descriptors(g_midiInHandle, g_pollFds, g_pollFdCount, POLLIN) != g_pollFdCount)
	{
		fprintf(stderr, "error: failed to populate pollfds\n");
		return false;
	}

	return true;
#endif
}

void CloseMidiDevice()
{
#ifdef _WIN32
	midiInClose(g_midiInHandle);
#else
	free(g_pollFds);
	snd_seq_port_subscribe_free(g_midiSubscription);
	snd_seq_unsubscribe_port(g_midiInHandle, g_midiSubscription);
	snd_seq_delete_simple_port(g_midiInHandle, g_midiPort);
	snd_seq_close(g_midiInHandle);
#endif
	g_midiInHandle = 0;
}

#if _WIN32
void PrintMidiDevices()
{
	uint32_t numDevices = (uint32_t)midiInGetNumDevs();
	printf("System has %u midi In devices\n", numDevices);

	for (uint32_t dev = 0; dev < numDevices; dev++)
	{
		MIDIINCAPS inCaps = {};
		midiInGetDevCaps((UINT_PTR)&dev, &inCaps, sizeof(MIDIINCAPS));
		printf("Device %u is \"%s\"\n", dev, inCaps.szPname);
	}
}
#endif

std::string g_configFileName;

#if _WIN32
FILETIME g_lastConfigWriteTimestamp = { 0 };

bool ConfigFileChanged()
{
	HANDLE h = CreateFile(g_configFileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		fprintf(stderr, "error: couldn't open %s\n", g_configFileName.c_str());
		return false;
	}

	FILETIME createTimestamp, writeTimestamp;
	GetFileTime(h, &createTimestamp, NULL, &writeTimestamp);

	CloseHandle(h);

	FILETIME latest;
	if (CompareFileTime(&createTimestamp, &writeTimestamp) < 0)
	{
		latest = writeTimestamp;
	} else {
		latest = createTimestamp;
	}


	if (CompareFileTime(&latest, &g_lastConfigWriteTimestamp) <= 0)
	{
		return false;
	}

	g_lastConfigWriteTimestamp = latest;

	return true;
}

#else

bool ConfigFileChanged()
{
	return false;
}

#endif

// a version of strtok that supports double quotes
char* tokenize(char* str, const char* delimiters)
{
	static char* next = NULL;
	if (str) next = str;

	while (*next)
	{
		if (!strchr(delimiters, *next))
			break;

		next++;
	}

	if (!*next)
		return NULL;

	if (*next == '"')
	{
		next++;
		delimiters = "\"";
	}

	char* start = next;

	while (*next)
	{
		if(strchr(delimiters, *next))
		{
			*next = 0;
			next++;
			break;
		}

		next++;
	}

	return start;
}

bool ReadConfigFile()
{
	struct KorgiConfig new_config = g_config;

	FILE* file = fopen(g_configFileName.c_str(), "r");
	if (!file)
	{
		fprintf(stderr, "error: couldn't open %s\n", g_configFileName.c_str());
		return false;
	}

	bool success = true;
	char linebuf[256];
	int lineno = 0;
	while (fgets(linebuf, sizeof(linebuf), file))
	{
		lineno++;

		{ char* t = strchr(linebuf, '#'); if (t) *t = 0; } // remove comments
		{ char* t = strchr(linebuf, '\n'); if (t) *t = 0; } // remove newline

		const char* delimiters = " \t\r\n";
		char* command = tokenize(linebuf, delimiters);

		if (!command)
			continue;

		if (strcmp(command, "connect") == 0)
		{
			char* addr = tokenize(nullptr, delimiters);
			char* port = tokenize(nullptr, delimiters);

			if (!addr)
			{
				fprintf(stderr, "%s:%d: insufficient parameters for 'connect'\n", g_configFileName.c_str(), lineno);
				success = false;
				continue;
			}

			new_config.address = addr;
			if (port) new_config.port = atoi(port);
		}
		else if (strcmp(command, "password") == 0)
		{
			char* password = tokenize(nullptr, delimiters);

			if (!password)
			{
				fprintf(stderr, "%s:%d: insufficient parameters for 'password'\n", g_configFileName.c_str(), lineno);
				success = false;
				continue;
			}

			new_config.password = password;
		}
		else if (strcmp(command, "device") == 0)
		{
			char* device = tokenize(nullptr, delimiters);

			if (!device)
			{
				fprintf(stderr, "%s:%d: insufficient parameters for 'device'\n", g_configFileName.c_str(), lineno);
				success = false;
				continue;
			}

			new_config.device = atoi(device);
		}
		else if (strcmp(command, "device_name") == 0)
		{
			char* device_name = tokenize(nullptr, delimiters);

			if (!device_name)
			{
				fprintf(stderr, "%s:%d: insufficient parameters for 'device_name'\n", g_configFileName.c_str(), lineno);
				success = false;
				continue;
			}

			new_config.device_name = device_name;
		}
		else if (strcmp(command, "device_map") == 0)
		{
			char* device_map = tokenize(nullptr, delimiters);

			if (!device_map)
			{
				fprintf(stderr, "%s:%d: insufficient parameters for 'device_map'\n", g_configFileName.c_str(), lineno);
				success = false;
				continue;
			}

			if (!setControlSurfaceType(device_map))
			{
				fprintf(stderr, "%s:%d: unsupported control surface type '%s'\n", g_configFileName.c_str(), lineno, device_map);
				success = false;
				continue;
			}
		}
		else if (strcmp(command, "button") == 0)
		{
			char* channel = tokenize(nullptr, delimiters);
			char* command = channel + strlen(channel) + 1;

			if (!channel || !*command)
			{
				fprintf(stderr, "%s:%d: insufficient parameters for 'button'\n", g_configFileName.c_str(), lineno);
				success = false;
				continue;
			}

			char *endptr = nullptr;
			int c = strtol(channel, &endptr, 10);
			if (endptr - channel != strlen(channel))
			{
				// invalid integer, try control surface alias
				ControlSurface surf;
				if (!mapControl(surf, channel))
				{
					fprintf(stderr, "%s:%d: invalid channel number or button alias '%s'\n", g_configFileName.c_str(), lineno, channel);
					success = false;
					continue;
				}

				if (surf.type != ControlSurface::Type::Button)
				{
					fprintf(stderr, "%s:%d: control surface '%s' is not a button\n", g_configFileName.c_str(), lineno, channel);
					success = false;
					continue;
				}

				new_config.buttons[surf.channel] = command;
			} else {
				new_config.buttons[c] = command;
			}
		}
		else if (strcmp(command, "knob") == 0 || strcmp(command, "slider") == 0)
		{
			bool isKnob = strcmp(command, "knob") == 0;

			char* channel = tokenize(nullptr, delimiters);
			char* cvar = tokenize(nullptr, delimiters);
			char* vmin = tokenize(nullptr, delimiters);
			char* vmax = tokenize(nullptr, delimiters);

			if (!channel || !cvar || !vmin || !vmax)
			{
				fprintf(stderr, "%s:%d: insufficient parameters for '%s'\n", g_configFileName.c_str(), lineno, command);
				success = false;
				continue;
			}

			KnobMapping mapping;
			mapping.name = cvar;
			mapping.min_value = float(atof(vmin));
			mapping.max_value = float(atof(vmax));

			char *endptr = nullptr;
			int c = strtol(channel, &endptr, 10);
			if (endptr - channel != strlen(channel))
			{
				// invalid integer, try control surface alias
				ControlSurface surf;
				if (!mapControl(surf, channel))
				{
					fprintf(stderr, "%s:%d: invalid channel number or %s alias '%s'\n", g_configFileName.c_str(), lineno, command, channel);
					success = false;
					continue;
				}

				if ((isKnob && surf.type != ControlSurface::Type::RotaryKnob) ||
					(!isKnob && surf.type != ControlSurface::Type::Slider))
				{
					fprintf(stderr, "%s:%d: control surface '%s' is not a %s\n", g_configFileName.c_str(), lineno, channel, command);
					success = false;
					continue;
				}

				new_config.knobs[surf.channel] = mapping;
			} else {
				new_config.knobs[c] = mapping;
			}
		}
		else
		{
			fprintf(stderr, "%s:%d: unknown directive '%s'\n", g_configFileName.c_str(), lineno, command);
			success = false;
			continue;
		}
	}

	fclose(file);

	if (new_config.password.size() == 0)
	{
		fprintf(stderr, "%s: password not specified\n", g_configFileName.c_str());
		success = false;
	}

	if (success)
	{
		printf("korgi: mapping %d knobs and %d buttons\n", int(new_config.knobs.size()), int(new_config.buttons.size()));
		g_config = new_config;
	}

	return success;
}

void SignalHandler(int signal)
{
	g_terminate = true;
}

void Run()
{
	while (!g_terminate)
	{
#ifdef _WIN32
		//Quiet spin
		Sleep(50);
#else
		for (int i = poll(g_pollFds, g_pollFdCount, 60*1000); i > 0; i--)
		{
			snd_seq_event_t *event;
			snd_seq_event_input(g_midiInHandle, &event);

			unsigned char event_chn = event->data.control.param;
			unsigned char event_val = event->data.control.value;

			HandleMidiInput(event_chn, event_val);

			snd_seq_free_event(event);
		}
#endif

		if (ConfigFileChanged())
		{
			fprintf(stderr, "reloading config file\n");
			ReadConfigFile();
		}
	}
}

int main(int argc, char** argv)
{
	g_configFileName = "korgi.conf";

	if (argc > 1)
		g_configFileName = argv[1];

	// initialize the config file timestamp
	ConfigFileChanged();

	if (!ReadConfigFile())
		return 1;

	if (!OpenSocket())
		return 1;

	if (!OpenMidiDevice())
		return 1;

	signal(SIGINT, SignalHandler);
	
	Run();

	printf("\n");
	printf("korgi: shutting down...\n");

	CloseMidiDevice();
	CloseSocket();

	return 0;
}

// vim: expandtab!:
