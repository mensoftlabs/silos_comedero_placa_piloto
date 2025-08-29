# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/danie/esp/v5.0/esp-idf/components/bootloader/subproject"
  "C:/Users/danie/OneDrive/Documentos/MENSOFT/SILOS/librerias/comedero_ESPpiloto/build/bootloader"
  "C:/Users/danie/OneDrive/Documentos/MENSOFT/SILOS/librerias/comedero_ESPpiloto/build/bootloader-prefix"
  "C:/Users/danie/OneDrive/Documentos/MENSOFT/SILOS/librerias/comedero_ESPpiloto/build/bootloader-prefix/tmp"
  "C:/Users/danie/OneDrive/Documentos/MENSOFT/SILOS/librerias/comedero_ESPpiloto/build/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/danie/OneDrive/Documentos/MENSOFT/SILOS/librerias/comedero_ESPpiloto/build/bootloader-prefix/src"
  "C:/Users/danie/OneDrive/Documentos/MENSOFT/SILOS/librerias/comedero_ESPpiloto/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/danie/OneDrive/Documentos/MENSOFT/SILOS/librerias/comedero_ESPpiloto/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/danie/OneDrive/Documentos/MENSOFT/SILOS/librerias/comedero_ESPpiloto/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
