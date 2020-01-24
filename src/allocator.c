static void *default_alloc(void *context, size_t size) {
  (void)context;
  return malloc(size);
}

static void default_free(void *context, void *pointer) {
  (void)context;
  free(pointer);
}

static const allocator_t default_allocator = {NULL, default_alloc, default_free};
