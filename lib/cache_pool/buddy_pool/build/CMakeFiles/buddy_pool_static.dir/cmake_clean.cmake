file(REMOVE_RECURSE
  "libbuddy_pool_static.a"
  "libbuddy_pool_static.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang CXX)
  include(CMakeFiles/buddy_pool_static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
