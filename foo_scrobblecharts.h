void filterTracksByArtist(const char * artist, pfc::list_base_t<metadb_handle_ptr> &p_data);
int getTrackIndexByTitle(const char * title, const pfc::list_base_t<metadb_handle_ptr> &p_data);
metadb_handle_ptr getTrackByTitle(const char * title, const pfc::list_base_t<metadb_handle_ptr> &p_data);
void getUrl(const char * url, pfc::string8 &page, abort_callback* abort = 0) throw(pfc::exception);
pfc::string8 getMainArtist(const pfc::list_base_const_t<metadb_handle_ptr> &data);
inline bool isTrackByArtist(const char * artist,metadb_handle_ptr &track);
