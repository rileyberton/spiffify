#include <getopt.h>


#include "api.h"
#include "key.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdlib.h>

#define false 0
#define true 1
#define SPIFFIFY_PLAYLIST_NAME "Spiffify"

char *playlist_name = NULL;
char *user_name = NULL;
char *password = NULL;
bool reset = false;
pthread_mutex_t g_mutex;
pthread_cond_t g_cond;
bool g_notify_do = 0;
sp_playlistcontainer *g_list_container;
sp_session* session = NULL;
int did_spiffify=0;
int can_exit=0;


void usage() {
}

static void tracks_added(sp_playlist *pl, sp_error error) {
//	printf("tracks_added\n");
}
static void tracks_removed(sp_playlist *pl, sp_error error) {
//	printf("tracks_removed\n");
}
static void tracks_moved(sp_playlist *pl, sp_error error) {
//	printf("tracks_moved\n");
}
static void playlist_renamed(sp_playlist *pl, sp_error error) {
//	printf("playlist_renamed\n");	
}

static sp_playlist_callbacks pl_callbacks = {
    .tracks_added = &tracks_added,
    .tracks_removed = &tracks_removed,
    .tracks_moved = &tracks_moved,
    .playlist_renamed = &playlist_renamed,
};


static void playlist_added(sp_playlistcontainer *pc, sp_playlist *pl,
                           int position, void *userdata) 
{
//	printf("playlist_added: %s\n", sp_playlist_name(pl));
	sp_playlist_add_callbacks(pl, &pl_callbacks, NULL);
}

static void playlist_removed(sp_playlistcontainer *pc, sp_playlist *pl,
                           int position, void *userdata) 
{
//	printf("playlist_removed\n");
}


static void container_loaded(sp_playlistcontainer* c, sp_error error) {
	g_list_container = c;
	if (did_spiffify == 1) {
		printf("Complete!\n");
		can_exit=1;
	}

}

static sp_playlistcontainer_callbacks pc_callbacks = {
    .playlist_added = &playlist_added,
    .playlist_removed = &playlist_removed,
    .container_loaded = &container_loaded,
};


static void logged_in(sp_session *session, sp_error error) {
	pthread_mutex_lock(&g_mutex);
	if (SP_ERROR_OK != error) {
		fprintf(stderr, "Could not perform login: %s\n", sp_error_message(error));
		exit(1);
	}
	pthread_cond_signal(&g_cond);
	pthread_mutex_unlock(&g_mutex);

	printf("Logged in!\n");

	sp_playlistcontainer* list_container = sp_session_playlistcontainer(session);
	if (list_container == NULL) {
		fprintf(stderr, "Cannot get main playlist container, aborting");
		exit(1);
	}


	sp_playlistcontainer_add_callbacks(
        list_container,
        &pc_callbacks,
        NULL);



}

static void notify_main_thread(sp_session *sess)
{
    pthread_mutex_lock(&g_mutex);
    g_notify_do = 1;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
}


static void connection_error(sp_session *session, sp_error error) {
	fprintf(stderr, "connection_error %s", sp_error_message(error));
}

static sp_playlist *find_album_playlist(sp_playlistcontainer *list_container, int *spiffify_start_index, int spiffify_end_index, sp_album *al) 
{
	if (al == NULL) {
		return NULL;
	}
	int j=*spiffify_start_index;
	sp_artist *a = sp_album_artist(al);
	const char *artist = sp_artist_name(a);
	int artist_end = 0;
	sp_uint64 artist_folder_id = 0;
	for(; j < spiffify_end_index; j++) {
		sp_playlist *p = sp_playlistcontainer_playlist(list_container, j);
		if (p == NULL) {
			continue;
		}
		sp_playlist_type type = sp_playlistcontainer_playlist_type(list_container,j);
		if (type == SP_PLAYLIST_TYPE_START_FOLDER) {
			char folder_name[256];
			sp_error error = sp_playlistcontainer_playlist_folder_name(list_container, j, folder_name, 255);
			if (strcmp(folder_name, artist) == 0) {
				artist_folder_id = sp_playlistcontainer_playlist_folder_id(list_container, j);
			}
		}
		else if (type == SP_PLAYLIST_TYPE_END_FOLDER) {
			if (artist_folder_id == sp_playlistcontainer_playlist_folder_id(list_container, j)) {
				artist_end = j;
			}
		}
		const char *plname = sp_playlist_name(p);
		if (plname != NULL) {
			const char *alname = sp_album_name(al);
			if (strcmp(plname, alname) == 0) {
				return p;
			}
		}
	}
	// in the case where we don't yet have the playlist for this album and we are about to create it, send back the end of this artist folder
	if (artist_end != 0) {
		*spiffify_start_index = artist_end-1;
	}
	return NULL;
}

static void remove_spiffify_list() {
}

static bool validate_complete_load() 
{
	if (g_list_container == NULL) {
		return false;
	}
	sp_playlistcontainer *list_container = g_list_container;	
	int pl_count = sp_playlistcontainer_num_playlists(list_container);
	int i=0;
	for(; i < pl_count; i++) {
		sp_playlist *l = sp_playlistcontainer_playlist(list_container, i);
		sp_playlist_type type = sp_playlistcontainer_playlist_type(list_container, i);
		if (type == SP_PLAYLIST_TYPE_PLAYLIST && !sp_playlist_is_loaded(l)) {
			return false;
		}
	}
	sp_playlist *source_list = NULL;
	i=0;
	for(; i < pl_count; i++) {
		sp_playlist *l = sp_playlistcontainer_playlist(list_container, i);
		const char* pname = sp_playlist_name(l);
		if (strcmp(pname, playlist_name) == 0) {
			source_list = l;
			break;
		}
	}
	
	if (source_list == NULL) {
		fprintf(stderr, "Cannot find source list: %s\n", playlist_name);
		exit(1);
	}
		
	int nt = sp_playlist_num_tracks(source_list);
	i=0;
	for(; i < nt; i++) {
		sp_track *t = sp_playlist_track(source_list, i);
		sp_artist *a = sp_track_artist(t,0); // get first artist on track because I am lazy
		sp_album *al = sp_track_album(t);
		if (al == NULL || a == NULL) {
			return false;
		}
	}
	return true;
}

int compare_tracks(const void* a, const void* b) {
	// type safety??! this is C god dammit!
	sp_track *left = *((sp_track**)a);
	sp_track *right = *((sp_track**)b);

	sp_artist *lefta = sp_track_artist(left,0); // get first artist on track because I am lazy
	sp_album *leftal = sp_track_album(left);

	sp_artist *righta = sp_track_artist(right, 0);
	sp_album *rightal = sp_track_album(right);

	const char *left_artist = sp_artist_name(lefta);
	const char *right_artist = sp_artist_name(righta);
	
	int artist_compare = strcmp(left_artist, right_artist);
	if (artist_compare != 0) {
		return artist_compare;
	}
	
	const char *left_album = sp_album_name(leftal);
	const char *right_album = sp_album_name(rightal);

	int album_compare = strcmp(left_album, right_album);
	if (album_compare != 0) {
		return album_compare;
	}

	// for the last tier we use track number, not name.
	int left_track = sp_track_index(left);
	int right_track = sp_track_index(right);
	return left < right ? -1 : left > right ? 1 : 0;

}

static bool spiffify() {
	if (!validate_complete_load()) {
		return false;
	}
	sp_playlistcontainer *list_container = g_list_container;	

	// check to see if all playlists have been loaded
	int pl_count = sp_playlistcontainer_num_playlists(list_container);
	int i=0;
	sp_playlist *source_list = NULL;
	sp_playlist *spiffify_list = NULL;
	char folder_name[256];
	int spiffify_start_index = -1;
	int spiffify_end_index = -1;
	sp_uint64 spiffify_folder_id = -1;
	i=0;
	for(; i < pl_count; i++) {
		sp_playlist *l = sp_playlistcontainer_playlist(list_container, i);
		const char* pname = sp_playlist_name(l);
		if (strcmp(pname, playlist_name) == 0) {
			source_list = l;
		}
		sp_playlist_type type = sp_playlistcontainer_playlist_type(list_container, i);
		if (type == SP_PLAYLIST_TYPE_START_FOLDER) {
			sp_error error = sp_playlistcontainer_playlist_folder_name(list_container, i, folder_name, 256);
			if (error == SP_ERROR_OK) {
				if (strcmp(folder_name, SPIFFIFY_PLAYLIST_NAME) == 0) {
					spiffify_list = l;
					spiffify_start_index = i;
					spiffify_folder_id = sp_playlistcontainer_playlist_folder_id(list_container, i);
				}
			}
		}
		else if (type == SP_PLAYLIST_TYPE_END_FOLDER) {
			sp_uint64 id = sp_playlistcontainer_playlist_folder_id(list_container, i);
			if (id == spiffify_folder_id) {
				spiffify_end_index = i;
			}
		}
	}

	if (source_list == NULL) {
		fprintf(stderr, "Cannot find source list: %s\n", playlist_name);
		exit(1);
	}

	if (spiffify_list != NULL && spiffify_start_index != -1) {
		// smoke the list and start over every time.. it's just not worth the effort of inserting changes.
		int x=spiffify_end_index;
		for(; x >= spiffify_start_index; x--) {
			sp_playlistcontainer_remove_playlist(list_container, x);
		}
		spiffify_list = NULL;
		spiffify_start_index = -1;
		spiffify_end_index = -1;
	}
	
	pl_count = sp_playlistcontainer_num_playlists(list_container); // reset count.

	if (spiffify_list == NULL) {
		// make the Spiffify list for this user;
		sp_error error = sp_playlistcontainer_add_folder(list_container, pl_count, SPIFFIFY_PLAYLIST_NAME);
		if (error == SP_ERROR_OK) {
			spiffify_list = sp_playlistcontainer_playlist(list_container, pl_count);
			spiffify_start_index = pl_count;
			spiffify_end_index = spiffify_start_index+1;
		}
		if (spiffify_list == NULL) {
			fprintf(stderr, "Cannot create '%s' playlist\n", SPIFFIFY_PLAYLIST_NAME);
			exit(1);
		}
	}
	
	// iterate the source playlist
	int nt = sp_playlist_num_tracks(source_list);

	// allocate storage for tracks
	sp_track **tracks = (sp_track **) malloc(nt * sizeof(sp_track *));
	i=0;
	for(; i < nt; i++) {
		sp_track *t = sp_playlist_track(source_list, i);
		tracks[i] = t; // store in the array
	}
	// :)
	mergesort(tracks, nt, sizeof(sp_track *), &compare_tracks);

	i=0;
	for(; i < nt; i++) {
		sp_track *t = tracks[i]; //sp_playlist_track(source_list, i);
		sp_artist *a = sp_track_artist(t,0); // get first artist on track because I am lazy
		// find artist folder inside our spiffify list
		bool haveartist = false;
		int j=spiffify_start_index;
		char artist[256];
		
		if (a != NULL) {
			strcpy(artist, sp_artist_name(a));
		}
		else {
			strcpy(artist, "Unknown Artist");
		}

		for(; j < spiffify_end_index; j++) {
			sp_error error = sp_playlistcontainer_playlist_folder_name(list_container, j, folder_name, 256);
			if (strcmp(artist, folder_name) == 0) {
				haveartist = true;
				break;
			}
		}
		sp_album *al = sp_track_album(t);
		while (al == NULL) {
			printf("Cannot find album or it's not loaded.. forcing an event loop\n");
			return false;
		}
		if (!haveartist) {
			int artist_pos = spiffify_end_index;
			sp_playlistcontainer_add_folder(list_container, artist_pos, artist);
			spiffify_end_index+=2; // a folder adds 2 indexes.
			pl_count = sp_playlistcontainer_num_playlists(list_container);
			sp_playlist* album = sp_playlistcontainer_add_new_playlist(list_container, sp_album_name(al));
			sp_playlist_add_tracks(album, &t, 1, 0, session);
			sp_playlistcontainer_move_playlist(list_container, pl_count, artist_pos+1, false);
			spiffify_end_index++; // one more for the album.
		}
		else {
			int artist_pos = j;
			sp_playlist *albumpl = find_album_playlist(list_container, &artist_pos, spiffify_end_index, al);
			if (albumpl == NULL) {
				pl_count = sp_playlistcontainer_num_playlists(list_container);
				albumpl = sp_playlistcontainer_add_new_playlist(list_container, sp_album_name(al));
				sp_playlistcontainer_move_playlist(list_container, pl_count, artist_pos+1, false);
				spiffify_end_index++; // one more for the album.
			}
			sp_playlist_add_tracks(albumpl, &t, 1, 0, session);
		}
	}
	free(tracks);
	return true;
}

int main(int argc, char** argv)
{
	for (;;) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "hl:u:p:r", NULL, &option_index);
		if (c == -1) {
			break ;
		}
		switch (c) {
		case 'h':
			usage();
			break;
		case 'l':
			playlist_name = strdup(optarg);
			break;
		case 'u':
			user_name = strdup(optarg);
			break;
		case 'p':
			password = strdup(optarg);
			break;
		case 'r':
			reset = true;
			break;
			
		};
	}

	// setup our spotify config
	sp_session_config config;
	memset(&config, 0, sizeof(config));
	config.api_version = SPOTIFY_API_VERSION;
	
	
	config.cache_location = "/tmp/spiffify";
	config.settings_location = "/tmp/spiffify";
	config.application_key = g_appkey;
	config.application_key_size = g_appkey_size;

	/* mkdir(config.cache_location, 0); */
	/* mkdir(config.settings_location, 0); */

	
	config.user_agent = "spiffify";

	sp_session_callbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.logged_in = &logged_in;
	/* callbacks.logged_out = logged_out; */
	callbacks.connection_error = &connection_error; 
	callbacks.notify_main_thread = &notify_main_thread;

	config.callbacks = &callbacks;

	// setup waits
	pthread_mutex_init(&g_mutex, NULL);
	pthread_cond_init(&g_cond, NULL);


	sp_error error = sp_session_create(&config, &session);
	if (SP_ERROR_OK != error) {
		fprintf(stderr, "Could not create session: %s\n", sp_error_message(error));
		abort();
	}

	error = sp_session_login(session, user_name, password, 0, NULL);
	if (SP_ERROR_OK != error) {
		fprintf(stderr, "Could not initiate login: %s\n", sp_error_message(error));
		abort();
	}
	pthread_mutex_lock(&g_mutex);


	int next_timeout=0;
	int load_completed=0;
	int messaged_load_waiting=0;
	for(;;) {
		if (next_timeout == 0) {
            while(!g_notify_do) {
                pthread_cond_wait(&g_cond, &g_mutex);
			}
		}
		else {
            struct timespec ts;

#if _POSIX_TIMERS > 0
            clock_gettime(CLOCK_REALTIME, &ts);
#else
            struct timeval tv;
            gettimeofday(&tv, NULL);
            TIMEVAL_TO_TIMESPEC(&tv, &ts);
#endif

            pthread_cond_timedwait(&g_cond, &g_mutex, &ts);
        }

        g_notify_do = 0;
        pthread_mutex_unlock(&g_mutex);

        do {
            sp_session_process_events(session, &next_timeout);
			if (load_completed == 0 && validate_complete_load()) {
				if (did_spiffify == 0 ) {
					spiffify();
					did_spiffify = 1;
				}
				sp_session_process_events(session, &next_timeout);
				// when the container_loaded event comes back (don't ask) we can be sure that our stuff made it to the server and we can exit there.
				if (can_exit==1) {
					goto done;
				}
			}
			else {
				if (messaged_load_waiting==0) {
					printf("Load not completed.. waiting\n");
					messaged_load_waiting = 1;
				}
			}
        } while (next_timeout == 0);

        pthread_mutex_lock(&g_mutex);
	}

done:
	error = sp_session_logout(session);
	if (SP_ERROR_OK != error) {
		fprintf(stderr, "Could not logout: %s\n", sp_error_message(error));
		exit(1);
	}
	error = sp_session_release(session);
	if (SP_ERROR_OK != error) {
		fprintf(stderr, "Could not destroy session: %s\n", sp_error_message(error));
		exit(1);
	}
};
