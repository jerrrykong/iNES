LOCAL_PATH:=$(call my-dir)


all_c_files_recursively = \
 $(eval src_files = $(wildcard $1/*.c)) \
 $(eval src_files = $(src_files:$(LOCAL_PATH)/%=%))$(src_files) \
 $(eval item_all = $(wildcard $1/*)) \
 $(foreach item, $(item_all) $(),\
  $(eval item := $(item:%.c=%)) \
  $(call all_c_files_recursively, $(item))\
 )

#complie so
include $(CLEAR_VARS)
LOCAL_CFLAGS+=-Werror -Wall -fPIC
LOCAL_LDLIBS+=-pthread
LOCAL_MODULE:=libinescore
LOCAL_SRC_FILES:=../../libinescore.c
LOCAL_SRC_FILES+=$(call all_c_files_recursively, $(LOCAL_PATH)/../../comm)
LOCAL_SRC_FILES+=$(call all_c_files_recursively, $(LOCAL_PATH)/../../core)	
LOCAL_C_INCLUDES:=$(LOCAL_PATH)/../../include
include $(BUILD_SHARED_LIBRARY)

