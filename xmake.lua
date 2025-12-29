add_rules("mode.debug", "mode.release")
package("cppjieba")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/yanyiwu/cppjieba")
    set_urls("https://github.com/yanyiwu/cppjieba.git", {submodules = true})
    
    on_install(function (package)
        os.cp("include/cppjieba", package:installdir("include"))
        if os.exists("deps/limonp/include/limonp") then
            os.cp("deps/limonp/include/limonp", package:installdir("include"))
        else
            -- 备用方案：如果目录结构不同，尝试直接复制 deps/limonp
            os.cp("deps/limonp", package:installdir("include"))
        end
    end)
package_end()

add_requires("cppjieba")

target("my_jieba_demo")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("cppjieba")
    set_languages("c++17")
    set_rundir("$(projectdir)")
    on_load(function (target)
        local dict_dir = path.join(os.projectdir(), "dict")
        if not os.exists(dict_dir) then
            print("⚠️  检测到缺少字典文件，正在自动从 GitHub 下载...")
            import("net.http")
            import("utils.archive")
            local url = "https://github.com/yanyiwu/cppjieba/archive/refs/heads/master.zip"
            local archive_file = "cppjieba_master.zip"
            local temp_dir = "temp_jieba_extract"
            http.download(url, archive_file)
            print("📦 正在解压资源...")
            archive.extract(archive_file, temp_dir)
            local source_dict = path.join(temp_dir, "cppjieba-master", "dict")
            os.mv(source_dict, dict_dir)
            os.rm(archive_file)
            os.rm(temp_dir)
            print("✅ 字典配置完成！")
        end
    end)
