function(add_project_warnings out_target)
  add_library(${out_target} INTERFACE)

  target_compile_options(${out_target} INTERFACE
      $<$<COMPILE_LANG_AND_ID:C,Clang,GNU,AppleClang>:-Wall;-Wextra;-pedantic;-Wno-missing-field-initializers>
      $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU,AppleClang>:-Wall;-Wextra;-pedantic-errors;-Wno-missing-field-initializers>
      $<$<COMPILE_LANG_AND_ID:C,MSVC>:/W4;/WX>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/WX>
  )
endfunction()
