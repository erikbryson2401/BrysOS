/* BrysOS: el código portado va dentro del núcleo; las macros de módulo no hacen nada */
#ifndef _BRYS_LINUX_MODULE_H
#define _BRYS_LINUX_MODULE_H

#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>

struct module;
#define THIS_MODULE ((struct module*)0)
#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_VERSION(x)
#define MODULE_FIRMWARE(x)
#define MODULE_ALIAS(x)
#define MODULE_DEVICE_TABLE(type, name)
#define MODULE_PARM_DESC(name, desc)
#define MODULE_INFO(tag, info)
#define MODULE_IMPORT_NS(ns)
#define module_param(name, type, perm)
#define module_param_named(name, value, type, perm)
#define module_param_array(name, type, nump, perm)
#define try_module_get(m) 1
#define module_put(m) do { } while (0)
#define __module_get(m) do { } while (0)
#define request_module(...) (-ENOSYS)
#define KBUILD_MODNAME "brysos"

#endif
