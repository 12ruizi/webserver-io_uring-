file(REMOVE_RECURSE
  "libslab_pool_static.a"
  "libslab_pool_static.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang CXX)
  include(CMakeFiles/slab_pool_static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
