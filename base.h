Arena *arena_create(size_t block_size);
void arena_destroy(Arena *arena);

void *arena_alloc(Arena *arena, size_t size);
void *arena_alloc_zero(Arena *arena, size_t count, size_t size);

void arena_reset(Arena *arena); // Reset arena for reuse (keeps memory)
void arena_clear(Arena *arena); // Free all blocks except the first


ArenaTemp arena_temp_begin(Arena *arena);
void arena_temp_end(ArenaTemp temp);

void arena_debug_print(const Arena *arena);

static inline Str S(const char *cstr);
static inline Str S_line(const char *cstr);

static inline Str str_from(Str base, i32 start);

static inline Str str_from_to(Str base, i32 start, i32 end);

static Str str_copy(Arena *arena, const char *cstr);

static Str str_copy_str(Arena *arena, Str s);

static Str str_format(Arena *arena, const char *fmt, ...);

static inline bool str_cmp(Str a, Str b);

static inline bool str_cmp_cstr(Str a, const char *b);

static inline Str str_builder_push(Arena *arena, StrBuilder *builder, Str str);


#endif
