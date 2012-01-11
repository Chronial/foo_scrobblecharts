#define VERSION "0.2.3"

#include "../_sdk/foobar2000/SDK/foobar2000.h"
#include "../_sdk/foobar2000/helpers/helpers.h"
#include <wininet.h>
#include "xmlParser.h"
#include <map>
#include <vector>
#include "foo_scrobblecharts.h"
#include <time.h>

/*

CONCEPT:
*songrating* und *artist-max-songrating* im Tag speichern, ermöglich offlinemodus.
Auswahl im Menü:
- immer online versuchen, offline nur wenn online nicht möglich.
- zwei context-menü Einträge darstellen - einen für online, einen für tags
-- Automatisch online versuchen, wenn tags nicht verfügbar.

winHTTP benutzen!

Match auf float umstellen

Spezielle Felder für den playlist namen: %artist%

+------------------------------------------------------------+
| 0 Show artistnames in contextmenu                          |
| 0 Don't use caching for last.fm feeds                      | <-- "don't", weil caching nicht garantiert wird
| +-Track priority (multiple matches for the same track)---+ |
| | ______________________________________________________ | |
| +--------------------------------------------------------+ |
| +-Artist Charts------------------------------------------+ |
| | 0 Automatically start playing the first song           | |
| | 0 Output a list of songs I don't have                  | | <-- bessere Benennung notwendig - output in die Konsole
| | Playlist Name: (empty for active) ____________________ | |
| +--------------------------------------------------------+ |
| +-Similar Artist PLaylists-------------------------------+ |
| | 0 Automatically start playing the first song           | |
| | 0 Output a list of artists I don't have                | | <-- bessere Benennung notwendig - output in die Konsole
| | Playlist Name: (empty for active) ____________________ | |
| | 0 include selected artist in playlist                  | |
| | Numer of artists used to create the playlist: ___      | |
| +--------------------------------------------------------+ |
+------------------------------------------------------------+


*/


typedef std::multimap<int,pfc::string8,std::greater<int>> descMap_t;

DECLARE_COMPONENT_VERSION( "Last.fm Chart Player", VERSION, 
   "Downloads charts from last.fm and creates a playlist according to them\n"
   "By Christian Fersch\n"
   __DATE__ " - " __TIME__ );

/* Encode funny chars -> %xx in newly allocated storage */
/* (preserves '/' !) */
// returns the number of characters written (without closing NULL)
pfc::string8 urlencode(const char *s) {
	const char *p;
	char *t = new char[strlen(s)*5+1];
	char *tp;
	if(t == NULL) {
		fprintf(stderr, "Serious memory error...\n");
		exit(1);
	}
	tp=t;
	for(p=s; *p; p++) {
		if (*p == '/') {
			memcpy(tp, "%252F", 5);
			tp += 5;
		} else if(*p == '&') {
			memcpy(tp, "%2526", 5);
			tp += 5;
		} else if(!((*p >= 'A' && *p <= 'Z') ||
			(*p >= 'a' && *p <= 'z') ||
			(*p >= '0' && *p <= '9') ||
			(*p == '_' ) ||
			(*p == '-' ))) {
				sprintf(tp, "%%%02X", (unsigned char)*p);
				tp += 3;
		} else {
			*tp = *p;
			tp++;
		}
	}

	*tp='\0';

	return pfc::string8(t);
};



class FileNotFound : public pfc::exception {
public:
	FileNotFound(): pfc::exception("Artist could not be found on Last.fm"){}
};

void getUrl(const char * url, pfc::string8 &page, abort_callback* abort) throw(pfc::exception){
	HINTERNET inet = 0;
	HINTERNET netUrl = 0;
	DWORD statusCode;
	DWORD statusCodeLength = sizeof(DWORD);
	page.reset();
	try {
		inet = InternetOpenA("foo_scrobblecharts/" VERSION " (http://chron.visiondesigns.de/foobar2000)",INTERNET_OPEN_TYPE_PRECONFIG,NULL,NULL,NULL);
		if (inet == NULL){
			throw pfc::exception("Could not open internet connection.");
			//console::error("foo_scrobblecharts: Could not open Internet Connection.");
			//return false;
		}
		netUrl = InternetOpenUrlA(inet,url,NULL,NULL,INTERNET_FLAG_NO_UI,NULL);
		if (netUrl == NULL){
			throw pfc::exception("Couldn't connect to Last.fm server.");
			//console::error("foo_scrobblecharts: Error opening Connection to Last.fm Server");
		}
		HttpQueryInfo(netUrl,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER ,&statusCode,&statusCodeLength,NULL);
		if (statusCode == HTTP_STATUS_NOT_FOUND){
			throw FileNotFound();
			//throw pfc::exception("lalala");
		} else if (statusCode != HTTP_STATUS_OK){
			throw pfc::exception("Last.fm server returned an error.");
		}
		if (abort != 0)
			abort->check();
		char inBuff[1025];
		DWORD readBytes;
		int a=0;
		while(InternetReadFile(netUrl,inBuff,1024,&readBytes)){
			if (readBytes == 0)
				break;
			if (abort != 0)
				abort->check();
			page.add_string(inBuff,readBytes);
		}
	} catch (pfc::exception& exception) {
		InternetCloseHandle(netUrl);
		InternetCloseHandle(inet);
		throw exception;
	}
	return;
}

struct strCmp {
	bool operator()( const char* s1, const char* s2 ) const {
		return (stricmp_utf8( s1, s2 ) < 0);
	}
};

pfc::string8 getMainArtist(const pfc::list_base_const_t<metadb_handle_ptr> &data){
	std::map<const char*, int, strCmp> artists;
	int n = data.get_count();

	static_api_ptr_t<metadb> db;
	db->database_lock();
	for (int i=0; i < n; i++){
		const file_info * fileInfo;
		if (data[i]->get_info_async_locked(fileInfo) && fileInfo->meta_exists("artist")){
			const char * artist = fileInfo->meta_get("artist",0);
			artists[artist]++;
		}
	}
	int maxCount = 0;
	const char * maxArtist = 0;
	std::map<const char*, int, strCmp>::iterator iter;
	for( iter = artists.begin(); iter != artists.end(); iter++ ) {
		if ((iter->second) > maxCount){
			maxCount = iter->second;
			maxArtist = iter->first;
		}
	}
	db->database_unlock();
	pfc::string8 retArtist(maxArtist);
	return retArtist;
}

// database needs to be locked for this to work
inline bool isTrackByArtist(const char * artist,metadb_handle_ptr &track){
	const file_info * fileInfo;
	if (track->get_info_async_locked(fileInfo)){
		for (int j=0; j < fileInfo->meta_get_count_by_name("artist"); j++){
			if(stricmp_utf8(fileInfo->meta_get("artist", j), artist) == 0){
				return true;
			}
		}
	}
	return false;
}

// database has to be locked before calling this function
void filterTracksByArtist(const char * artist, pfc::list_base_t<metadb_handle_ptr> &p_data){
	t_size n = p_data.get_count();
	bit_array_bittable deleteMask(n);
	for (int i = 0; i < n; i++){
		const file_info * fileInfo;
		deleteMask.set(i,!isTrackByArtist(artist,p_data[i]));
	}
	p_data.remove_mask(deleteMask);
}


// database has to be locked before calling this function
int getTrackIndexByTitle(const char * title, const pfc::list_base_t<metadb_handle_ptr> &p_data){
	int track = -1;
	int trackPlaycount = -1;
	int trackBitrate = -1;
	t_size n = p_data.get_count();
	int j = 0;
	do {
		//if (j == 1)
		//	console::printf("second round: %s",title);
		for (int i = 0; i < n; i++){
			const file_info * fileInfo;
			if (p_data[i]->get_info_async_locked(fileInfo) && fileInfo->meta_exists("title")){
				bool match;
				if (j==0){
					match = (stricmp_utf8(fileInfo->meta_get("title", 0), title) == 0);
				} else {
					const char * songTitle = fileInfo->meta_get("title", 0);
					int end = strcspn(songTitle,"([");
					if (end != ~0){
						match = ((stricmp_utf8_ex(songTitle,end-1,title,~0) == 0)
							|| (stricmp_utf8_ex(songTitle,end,title,~0) == 0));
					}
				}
				if (match){
					int thisBitrate = fileInfo->info_get_bitrate();
					int thisPlaycount = 0;
					if (fileInfo->meta_exists("PLAY_COUNTER"))
						thisPlaycount = atoi(fileInfo->meta_get("PLAY_COUNTER",0));
					if (thisPlaycount > trackPlaycount){
						track = i;
						trackPlaycount = thisPlaycount;
						trackBitrate = thisBitrate;
					} else if (thisPlaycount == trackPlaycount){
						
						if (thisBitrate > trackBitrate){
							track = i;
							trackPlaycount = thisPlaycount;
							trackBitrate = thisBitrate;
						}
					}
				}
			}
		}
		j++;
	} while (track == -1 && j < 2);
	return track;
}
// database has to be locked before calling this function
metadb_handle_ptr getTrackByTitle(const char * title, const pfc::list_base_t<metadb_handle_ptr> &p_data){
	metadb_handle_ptr track = 0;
	int trackIndex = getTrackIndexByTitle(title,p_data);
	if (trackIndex != -1)
		track = p_data[trackIndex];
	return track;
}
descMap_t getArtistChart(pfc::string8 artist, abort_callback* abort = 0) throw (pfc::exception) {
	descMap_t trackList;
	pfc::string8 page;
	pfc::string8 url;
	url << "http://ws.audioscrobbler.com/1.0/artist/" << urlencode(artist) << "/toptracks.xml";
	getUrl(url, page, abort);

	if (abort != 0)
		abort->check();
	if (page.get_length() < 10)
		throw pfc::exception("Last.fm returned an empty page.\nThis is usually the result of a server overload. Try again later");
		
	pfc::string8 lastFmArtist;
	{
		t_size pageWSize = pfc::stringcvt::estimate_utf8_to_wide(page,~0);
		wchar_t * pageW = new wchar_t[pageWSize];
		pfc::stringcvt::convert_utf8_to_wide(pageW,pageWSize,page,~0);
		XMLNode rootNode = XMLNode::parseString(pageW,L"mostknowntracks");
		delete [] pageW;
		pfc::stringcvt::string_utf8_from_wide lastFmArtistConverter (rootNode.getAttribute(L"artist"));
		lastFmArtist = lastFmArtistConverter;
		//console::printf("lastFmArtist: %s",lastFmArtist.get_ptr());
		int trackListLength = rootNode.nChildNode(L"track");
		int maxReach = -1;
		for (int i=0; i < trackListLength; i++){
			XMLNode trackNode = rootNode.getChildNode(L"track",i);
			XMLNode nameNode = trackNode.getChildNode(L"name");
			XMLNode reachNode = trackNode.getChildNode(L"reach");
			if (nameNode.nText() != 1 || reachNode.nText() != 1){
				throw pfc::exception("Data Feed is invalid. This is a Last.fm Server error.");
			}
			pfc::stringcvt::string_utf8_from_wide trackNameConverter (nameNode.getText());
			int reach = atoi(pfc::stringcvt::string_utf8_from_wide(reachNode.getText()));
			if (maxReach < 0)
				maxReach = reach;
			reach = (reach * 10000) / maxReach;
			trackList.insert(std::pair<int,pfc::string8>(reach,trackNameConverter.get_ptr()));
		}
	}
	if (trackList.size() < 2){
		pfc::string8_fast msg(artist);
		msg += "'s chart listing is empty";
		throw pfc::exception(msg);
	}
	return trackList;
}


descMap_t getSimilarArtistChart(pfc::string8 artist, abort_callback* abort = 0){
	descMap_t artistList;
	pfc::string8 page;
	pfc::string8 url;
	url << "http://ws.audioscrobbler.com/1.0/artist/" << urlencode(artist) << "/similar.xml";
	getUrl(url, page, abort);
	if (abort != 0)
		abort->check();
	if (page.get_length() < 10)
		throw pfc::exception("Last.fm returned an empty page.\nThis is usually the result of a server overload. Try again later");
		
	pfc::string8 lastFmArtist;
	{
		t_size pageWSize = pfc::stringcvt::estimate_utf8_to_wide(page,~0);
		wchar_t * pageW = new wchar_t[pageWSize];
		pfc::stringcvt::convert_utf8_to_wide(pageW,pageWSize,page,~0);
		XMLNode rootNode = XMLNode::parseString(pageW,L"similarartists");
		delete [] pageW;
		pfc::stringcvt::string_utf8_from_wide lastFmArtistConverter (rootNode.getAttribute(L"artist"));
		lastFmArtist = lastFmArtistConverter;
		console::printf("lastFmArtist: %s",lastFmArtist.get_ptr());
		int artistListLength = rootNode.nChildNode(L"artist");
		for (int i=0; i < artistListLength; i++){
			XMLNode artistNode = rootNode.getChildNode(L"artist",i);
			XMLNode nameNode = artistNode.getChildNode(L"name");
			XMLNode matchNode = artistNode.getChildNode(L"match");
			if (nameNode.nText() != 1){
				throw pfc::exception("Data Feed is invalid. This is a Last.fm Server error.");
			}
			int match;
			if (matchNode.nText() != 1){
				match = 100 - (50/artistListLength)*i;
			} else {
				match = atoi(pfc::stringcvt::string_utf8_from_wide(matchNode.getText()));
			}
			pfc::stringcvt::string_utf8_from_wide artistNameConverter(nameNode.getText());
			artistList.insert(std::pair<int,pfc::string8>(match,pfc::string8(artistNameConverter)));
		}
	}
	if (artistList.size() < 2){
		pfc::string8_fast msg(artist);
		msg += "'s similar artist listing is empty";
		throw pfc::exception(msg);
	}
	artistList.insert(std::pair<int,pfc::string8>(101,artist));
	/*console::printf("----------- Similar for %s ----------------",(const char *)lastFmArtist);
	descMap_t::iterator iter;
	for( iter = artistList.begin(); iter != artistList.end(); iter++ ) {
		console::printf("%d: %s",iter->first,(const char *)iter->second);
	}*/
	return artistList;
}




class ArtistSimilarPlaylistGenerator : public threaded_process_callback {
private:
	pfc::string8 artist;
	pfc::list_t<metadb_handle_ptr> library;
	pfc::list_t<metadb_handle_ptr> tracks;
	bool success;
	bool useArtistCharts;
public:
	ArtistSimilarPlaylistGenerator (const char * artist, bool useArtistCharts){
		this->artist = artist;
		this->success = false;
		this->useArtistCharts = useArtistCharts;
	}
	virtual void on_init(HWND p_wnd){
		static_api_ptr_t<library_manager> lm;
		lm->get_all_items(library);
	}
	virtual void run(threaded_process_status& p_status, abort_callback& p_abort){
		try {
			p_status.set_progress_float(0);
			p_status.set_item("Downloading similar artist list from Last.Fm...");
			p_status.force_update();

			descMap_t artistList = getSimilarArtistChart(artist, &p_abort);
			p_abort.check();
								// 0->artist 1->track
			std::multimap<int, std::vector<pfc::string8>, std::greater<int>> trackList;
			std::multimap<int, std::vector<pfc::string8>, std::greater<int>>::iterator tlIter;

			std::map<const char*, pfc::list_t<metadb_handle_ptr>, strCmp> artistData; 
			std::map<const char*, pfc::list_t<metadb_handle_ptr>, strCmp>::iterator finder;

			int t = 0;
			int j = 0;
			bit_array_bittable useArtists(artistList.size());
			for (descMap_t::iterator it = artistList.begin(); it != artistList.end(); it++){
				j++;
				pfc::list_t<metadb_handle_ptr> dataSet = library;
				filterTracksByArtist(it->second,dataSet);
				if (dataSet.get_size() > 0){
					artistData.insert(std::pair<const char*,pfc::list_t<metadb_handle_ptr>>(it->second,dataSet));
					useArtists.set(j,true);
					if (++t > 19)
						break;
				}
			}
			if (useArtistCharts){
				j = 0;
				int k = 0;
				for (descMap_t::iterator it = artistList.begin(); it != artistList.end(); it++){
					j++;
					if (!useArtists[j])
						continue;
					k++;
					p_status.set_progress(k, t+2);
					pfc::string8 statusText;
					statusText << "Downloading artist charts for " << it->second << " (" << k << "/" << t << ")";
					p_status.set_item(statusText);
					try {
						descMap_t tList = getArtistChart(it->second, &p_abort);
						for (descMap_t::iterator jt = tList.begin(); jt != tList.end(); jt++){
							std::vector<pfc::string8> track;
							track.insert(track.end(),it->second);
							track.insert(track.end(),jt->second);
							int index = jt->first * it->first;
							trackList.insert(std::pair<int,std::vector<pfc::string8>>(index,track));
						}
					} catch (pfc::exception& e){
						console::printf("foo_scrobblecharts: Error getting Charts for %s: %s",(const char*)it->second,e.what());
					}
					p_abort.check();
				}
			}

			p_status.set_item("Generating Playlist...");
			p_status.set_progress_float(1);
			p_status.force_update();

			if (useArtistCharts){
				static_api_ptr_t<metadb> db;
				db->database_lock();
				for(tlIter = trackList.begin(); tlIter != trackList.end(); tlIter++ ) {
					pfc::list_t<metadb_handle_ptr> dataSet;
					pfc::string8 * trackArtist = &tlIter->second[0];
					pfc::string8 * trackTitle = &tlIter->second[1];
					if ((finder = artistData.find(*trackArtist)) != artistData.end()){
						dataSet = finder->second;
					} else {
						throw std::exception("unawaited exception");
					}
					metadb_handle_ptr track = getTrackByTitle(*trackTitle, dataSet);
					if (track != 0){
						tracks.add_item(track);
					}
				}
				db->database_unlock();
			} else {
				pfc::string8 ** pMap = new pfc::string8* [t*101];
				int pPos = 0;
				j = 0;
				for (descMap_t::iterator it = artistList.begin(); it != artistList.end(); it++){
					j++;
					if (!useArtists[j])
						continue;
					for (int i=0; i < it->first; i++){
						pMap[pPos] = &it->second;
						pPos++;
					}
				}
				srand(time(NULL));
				for (int i=0; i < 70; i++){
					pfc::list_t<metadb_handle_ptr> dataSet;
					int p = (rand() % pPos);
					if ((finder = artistData.find(*pMap[p])) != artistData.end()){
						dataSet = finder->second;
					} else {
						throw std::exception("unawaited exception");
					}
					if (dataSet.get_count() > 0) {
						int x = (rand() % dataSet.get_count());
						tracks.add_item(dataSet[x]);
						dataSet.remove_by_idx(x);
					}
				}
				delete[] pMap;
			}
			if (tracks.get_count() < 2){
				throw pfc::exception("Did not find enough tracks to make a playlist");
			}
			success = true;
			p_abort.check();
		} catch (exception_aborted e) {
			success = false;
		} catch (pfc::exception& e) {
			popup_message::g_show(e.what(),"foo_scrobblecharts: Error",popup_message::icon_error);
		}
	}
	virtual void on_done(HWND p_wnd,bool p_was_aborted) {
		if (!success)
			return;
		static_api_ptr_t<playlist_manager> pm;
		//const char * playlistName = artist;
		const char * playlistName = "#ArtistSimilarCharts";
		t_size playlist = pm->find_playlist(playlistName,~0);
		if (playlist != ~0){
			pm->playlist_undo_backup(playlist);
			pm->playlist_clear(playlist);
		} else {
			playlist = pm->create_playlist(playlistName,~0,~0);
		}
		pm->playlist_add_items(playlist,tracks,bit_array_true());
		pm->set_active_playlist(playlist);
		pm->set_playing_playlist(playlist);
		static_api_ptr_t<playback_control> pc;
		pc->start();
	}
};


void generateArtistSimilarPlaylist(const pfc::list_base_const_t<metadb_handle_ptr> &tracks, bool useArtistCharts){
	pfc::string8 artist = getMainArtist(tracks);
	if (artist.get_length() > 0){
		service_impl_t<ArtistSimilarPlaylistGenerator> generator(artist, useArtistCharts);
		static_api_ptr_t<threaded_process> tp;
		pfc::string8 title("Generating Similar Artists Playlist for ");
		title += artist;
		tp->run_modeless(&generator, tp->flag_show_abort | tp->flag_show_item | tp->flag_show_progress, core_api::get_main_window(), title, ~0);
	} else {
		console::error("no Artist Information found");
	}
}



class ArtistPlaylistGenerator : public threaded_process_callback {
private:
	pfc::string8 artist;
	pfc::list_t<metadb_handle_ptr> library;
	pfc::list_t<metadb_handle_ptr> tracks;
	bool success;
public:
	ArtistPlaylistGenerator (const char * artist){
		this->artist = artist;
		this->success = false;
	}
	virtual void on_init(HWND p_wnd){
		static_api_ptr_t<library_manager> lm;
		lm->get_all_items(library);
	}
	virtual void run(threaded_process_status& p_status, abort_callback& p_abort){
		try {
			p_status.set_item("Downloading chart listing from Last.Fm...");
			p_status.force_update();

			descMap_t trackList = getArtistChart(artist, &p_abort);
			p_abort.check();
			p_status.set_item("Generating Playlist...");
			p_status.force_update();

			static_api_ptr_t<metadb> db;
			db->database_lock();
			filterTracksByArtist(artist,library);
			for(descMap_t::iterator iter = trackList.begin(); iter != trackList.end(); iter++ ) {
				metadb_handle_ptr track = getTrackByTitle(iter->second, library);
				if (track != 0){
					tracks.add_item(track);
				}
			}
			db->database_unlock();
			if (tracks.get_count() < 2){
				throw pfc::exception("Did not find enough tracks to make a playlist");
			}
			success = true;
			p_abort.check();
		} catch (exception_aborted e) {
			success = false;
		} catch (pfc::exception& e) {
			popup_message::g_show(e.what(),"foo_scrobblecharts: Error",popup_message::icon_error);
		}
	}
	virtual void on_done(HWND p_wnd,bool p_was_aborted) {
		if (!success)
			return;
		static_api_ptr_t<playlist_manager> pm;
		//const char * playlistName = artist;
		const char * playlistName = "#ArtistCharts";
		t_size playlist = pm->find_playlist(playlistName,~0);
		if (playlist != ~0){
			pm->playlist_undo_backup(playlist);
			pm->playlist_clear(playlist);
		} else {
			playlist = pm->create_playlist(playlistName,~0,~0);
		}
		pm->playlist_add_items(playlist,tracks,bit_array_true());
		pm->set_active_playlist(playlist);
		pm->set_playing_playlist(playlist);
		static_api_ptr_t<playback_control> pc;
		pc->start();
	}
};


void generateArtistPlaylist(const pfc::list_base_const_t<metadb_handle_ptr> &tracks){
	pfc::string8 artist = getMainArtist(tracks);
	if (artist.get_length() > 0){
		service_impl_t<ArtistPlaylistGenerator> generator(artist);
		static_api_ptr_t<threaded_process> tp;
		pfc::string8 title("Generating Charts Playlist for ");
		title += artist;
		tp->run_modeless(&generator, tp->flag_show_abort | tp->flag_show_item, core_api::get_main_window(), title, ~0);
	} else {
		console::error("no Artist Information found");
	}
}

class PlaylistSortWorker : public threaded_process_callback{
private:
	bool success;
	pfc::list_t<metadb_handle_ptr> selectedItems;
	t_size activePlaylist;
	t_size playlistLength;
	t_size * newOrder;
	t_size * selectArray;
	int firstPos;
	int chartOrderCount;
	pfc::string8 mainArtist;
public:
	PlaylistSortWorker(){
		success = false;
	}
	virtual void on_init(HWND p_wnd){
		static_api_ptr_t<playlist_manager> pm;
		pm->activeplaylist_get_selected_items(selectedItems);
		mainArtist = getMainArtist(selectedItems);
		activePlaylist = pm->get_active_playlist();
		playlistLength = pm->activeplaylist_get_item_count();

		bit_array_bittable selectMask(playlistLength);
		pm->activeplaylist_get_selection_mask(selectMask);

		static_api_ptr_t<metadb> db;
		db->database_lock();
		for (int i=0; i < playlistLength; i++){
			metadb_handle_ptr t;
			if (selectMask[i]){
				pm->activeplaylist_get_item_handle(t, i);
				if (!isTrackByArtist(mainArtist,t)){
					selectMask.set(i,false);
				}
			}
		}

		pm->activeplaylist_set_selection(bit_array_true(),selectMask);
		selectedItems.remove_all();
		pm->activeplaylist_get_selected_items(selectedItems);
		db->database_unlock();

		int selectArraySize = selectedItems.get_count();
		// array of indexes of selected tracks
		selectArray = new t_size[selectArraySize];
		int c = 0;
		for (int i=0; i < playlistLength; i++){
			if (selectMask[i]){
				selectArray[c] = i;
				c++;
			}
		}
	}
	virtual void run(threaded_process_status & p_status,abort_callback & p_abort){
		try {
			static_api_ptr_t<metadb> db;

			p_status.set_item("Downloading chart listing from Last.Fm...");
			p_status.force_update();
			descMap_t trackList = getArtistChart(mainArtist, &p_abort);
			p_abort.check();
			p_status.set_item("Sorting selected items...");
			p_status.force_update();
			db->database_lock();

			int * chartOrder = new int[selectedItems.get_count()];
			chartOrderCount = 0;

			bit_array_bittable inChart(playlistLength);

			for (descMap_t::iterator it = trackList.begin(); it != trackList.end(); it++){
				int trackIndex = getTrackIndexByTitle(it->second,selectedItems);
				if (trackIndex != -1 && !inChart[selectArray[trackIndex]]){
					chartOrder[chartOrderCount] = selectArray[trackIndex];
					chartOrderCount++;
					inChart.set(selectArray[trackIndex],true);
				}
			}
			db->database_unlock();
			
			newOrder = new t_size[playlistLength];
			firstPos = selectArray[0];
			int offset = chartOrderCount;
			for (int i=0; i < playlistLength; i++){
				int chartOrderIndex =  i - firstPos;
				if (chartOrderIndex < 0){
					newOrder[i] = i;
				} else if ( chartOrderIndex < chartOrderCount){
					newOrder[i] = chartOrder[chartOrderIndex];
				} else {
					while(inChart[i-offset])
						offset--;
					newOrder[i] = i-offset;
				}
			}
			delete[] chartOrder;
			p_abort.check();
			success = true;
		} catch (pfc::exception& e) {
			popup_message::g_show(e.what(),"foo_scrobblecharts: Error",popup_message::icon_error);
		}
	}
	virtual void on_done(HWND p_wnd,bool p_was_aborted) {
		if (success){
			static_api_ptr_t<playlist_manager> pm;
			pm->playlist_reorder_items(activePlaylist,newOrder,playlistLength);
			pm->playlist_set_selection(activePlaylist,bit_array_true(),bit_array_range(firstPos,chartOrderCount));
			pm->playlist_set_focus_item(activePlaylist,firstPos);
		}
		delete[] newOrder;
		delete[] selectArray;
	}
};
void sortByCharts(){
	service_impl_t<PlaylistSortWorker> worker;
	static_api_ptr_t<threaded_process> tp;
	tp->run_modal(&worker, tp->flag_show_abort | tp->flag_show_item, core_api::get_main_window(), "Sorting by artist charts", ~0);
}


class my_contextmenu : public contextmenu_item_simple {
   virtual unsigned get_num_items(){
	   return 4;
   };
   virtual void get_item_default_path(unsigned p_index,pfc::string_base & p_out){
	   p_out = "Last.fm";
   };
   virtual void context_command(unsigned p_index,const pfc::list_base_const_t<metadb_handle_ptr> & p_data,const GUID & p_caller){
	   if (p_index == 0) {
		   if (p_data.get_count() > 0)
			   generateArtistPlaylist(p_data);
	   } else if (p_index == 1) {
		   if (p_caller == contextmenu_item::caller_playlist)
			   sortByCharts();
	   } else if (p_index == 2) {
		   if (p_data.get_count() > 0)
			   generateArtistSimilarPlaylist(p_data, true);
	   } else if (p_index == 3) {
		   if (p_data.get_count() > 0)
			   generateArtistSimilarPlaylist(p_data, false);
	   }
   };
   virtual GUID get_item_guid(unsigned p_index){
	   // {BC67ADA6-CF35-4101-8630-6DBBFE5BCEA5}
	   static const GUID guid_generateCharts = { 0xbc67ada6, 0xcf35, 0x4101, { 0x86, 0x30, 0x6d, 0xbb, 0xfe, 0x5b, 0xce, 0xa5 } };
	   
	   // {239CF925-EA60-44a6-A69C-61F4D77CC5F3}
	   static const GUID guid_sortByChart = { 0x239cf925, 0xea60, 0x44a6, { 0xa6, 0x9c, 0x61, 0xf4, 0xd7, 0x7c, 0xc5, 0xf3 } };

	   // {D947F29E-0DE4-48e2-997F-1CCB88FB01FE}
	   static const GUID guid_similarCharts = { 0xd947f29e, 0xde4, 0x48e2, { 0x99, 0x7f, 0x1c, 0xcb, 0x88, 0xfb, 0x1, 0xfe } };

	   // {A0D6192D-1C08-4d54-84A6-F775782696B4}
	   static const GUID guid_similarShuffle = { 0xa0d6192d, 0x1c08, 0x4d54, { 0x84, 0xa6, 0xf7, 0x75, 0x78, 0x26, 0x96, 0xb4 } };


	   if (p_index == 0)
		   return guid_generateCharts;
	   else if (p_index == 1)
		   return guid_sortByChart;
	   else if (p_index == 2)
		   return guid_similarCharts;
	   else if (p_index == 3)
		   return guid_similarShuffle;
   }
   virtual bool get_item_description(unsigned p_index,pfc::string_base & p_out){
	   if (p_index == 0)
		   p_out = "Generates a playlist from the Artist's charts on Last.fm";
	   else if (p_index == 1)
		   p_out = "Sorts the tracks according to the Artist's charts on Last.fm";
	   else if (p_index == 2)
		   p_out = "Generates a playlist from the Artist's similar artist charts on Last.fm";
	   else if (p_index == 3)
		   p_out = "Generates a playlist from the Artist's similar artist listing using shuffle";
	   return true;
   }
   virtual void get_item_name(unsigned p_index,pfc::string_base & p_out){
	   if (p_index == 0)
		   p_out = "Artist Charts Playlist";
	   else if (p_index == 1)
		   p_out = "Sort by Artist Charts";
	   else if (p_index == 2)
		   p_out = "Similar Charts Playlist";
	   else if (p_index == 3)
		   p_out = "Similar Shuffle Playlist";
   };
   virtual bool context_get_display(unsigned p_index,const pfc::list_base_const_t<metadb_handle_ptr> & p_data,pfc::string_base & p_out,unsigned & p_displayflags,const GUID & p_caller) {
	   if (p_index == 0){
		   p_out = getMainArtist(p_data);
		   t_size len = p_out.get_length();
		   if (len > 0){
			   if ((p_out.get_ptr())[len-1] == 's')
				   p_out.add_string("' Charts Playlist");
			   else
				   p_out.add_string("'s Charts Playlist");
		   } else {
			   get_item_name(p_index,p_out);
		   }
		   return true;
	   } else if (p_index == 1) {
		   if (p_caller == contextmenu_item::caller_playlist){
			   pfc::string8 mainArtist = getMainArtist(p_data);
			   t_size len = mainArtist.get_length();
			   if (len > 0){
				   p_out = "Sort by ";
				   p_out += (const char*)mainArtist;
				   if ((mainArtist.get_ptr())[len-1] == 's')
					   p_out += "' Charts";
				   else
					   p_out += "'s Charts";
			   } else {
				   get_item_name(p_index,p_out);
			   }
			   return true;
		   } else {
			   return false;
		   }
	   } else if (p_index == 2){
		   p_out = getMainArtist(p_data);
		   t_size len = p_out.get_length();
		   if (len > 0){
			   if ((p_out.get_ptr())[len-1] == 's')
				   p_out.add_string("' Similar Charts Playlist");
			   else
				   p_out.add_string("'s Similar Charts Playlist");
		   } else {
			   get_item_name(p_index,p_out);
		   }
		   return true;
	   } else if (p_index == 3){
		   p_out = getMainArtist(p_data);
		   t_size len = p_out.get_length();
		   if (len > 0){
			   if ((p_out.get_ptr())[len-1] == 's')
				   p_out.add_string("' Similar Shuffle Playlist");
			   else
				   p_out.add_string("'s Similar Shuffle Playlist");
		   } else {
			   get_item_name(p_index,p_out);
		   }
		   return true;
	   } else {
		   get_item_name(p_index,p_out);
		   return true;
	   }
   }
};


static contextmenu_item_factory_t< my_contextmenu > foo_contextmenu;