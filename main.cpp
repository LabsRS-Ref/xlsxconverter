#include <functional>
#include <thread>
#include <mutex>

#include "arg_config.hpp"
#include "yaml_config.hpp"
#include "handlers.hpp"
#include "converter.hpp"

#define EXCEPTION XLSXCONVERTER_UTILS_EXCEPTION

namespace {
using namespace xlsxconverter;

struct Task
{
    struct RelationYaml {
        std::string id;
        YamlConfig yaml_config;
        YamlConfig::Field::Relation relation;
        inline
        RelationYaml(std::string id, YamlConfig yaml_config, YamlConfig::Field::Relation relation)
            : yaml_config(yaml_config),
              relation(relation)
        {}
    };
    using lock_guard = std::lock_guard<std::mutex>;
    utils::mutex_list<std::string, utils::spinlock> targets;
    utils::mutex_list<YamlConfig, utils::spinlock> yaml_configs;
    utils::mutex_list<YamlConfig::Field::Relation, utils::spinlock> relations;
    utils::mutex_list<RelationYaml, utils::spinlock> relation_yamls;

    ArgConfig& arg_config;
    bool canceled;
    std::mutex phase1_done;
    std::mutex phase2_done;
    std::mutex phase3_done;
    std::atomic_int phase1_running;
    std::atomic_int phase2_running;
    std::atomic_int phase3_running;

    Task(ArgConfig& arg_config, int jobs)
        : canceled(false),
          arg_config(arg_config)
    {
        if (arg_config.targets.empty() && arg_config.yaml_search_path != ".") {
            for (auto& target: utils::fs::walk(arg_config.yaml_search_path, "*.yaml")) {
                targets.push_back(target);
            }
        } else {
            for (auto& target: arg_config.targets) {
                targets.push_back(target);
            }
        }
        phase1_done.lock();
        phase2_done.lock();
        phase3_done.lock();
        phase1_running = jobs;
        phase2_running = jobs;
        phase3_running = jobs;
    }

    void run() {
        // yaml
        phase1();
        if (canceled) return;
        { lock_guard lock(phase1_done); };

        // relation
        phase2();
        if (canceled) return;
        { lock_guard lock(phase2_done); };

        // relation mapping
        phase3();
        if (canceled) return;
        { lock_guard lock(phase3_done); };

        // convert
        phase4();
    }

    struct id_functor {
        std::string id;
        inline id_functor(std::string id) : id(id) {};
        template<class T> bool operator()(T& t) { return t.id == id; }
    };

    void phase1() {
        while (!canceled) {
            auto target = targets.move_front();
            if (target == boost::none) break;

            auto yaml_config = YamlConfig(target.value(), arg_config);
            for (auto rel: yaml_config.relations()) {
                std::string fullpath = utils::fs::joinpath(arg_config.yaml_search_path, rel.from);
                if (!utils::fs::exists(fullpath)) {
                    throw EXCEPTION(target.value(), ": relational_yaml=", fullpath, " does not exist.");
                }
                if (!relations.any(id_functor(rel.id))) {
                    relations.push_back(std::move(rel));
                }
            }
            yaml_configs.push_back(std::move(yaml_config));
        }
        --phase1_running;
        if (phase1_running.load() == 0) {
            if (relations.empty()) {
                phase3_done.unlock();
                phase2_done.unlock();
            }
            phase1_done.unlock();
        }
    }
    void phase2() {
        while (!canceled) {
            bool last = false;
            auto relation = relations.move_front(&last);
            if (relation == boost::none) break;

            // if (!arg_config.quiet) {
            //     utils::log("rel_yaml: ", relation->from);
            // }
            auto yaml_config = YamlConfig(relation->from, arg_config);
            for (auto rel: yaml_config.relations()) {
                if (!relations.any(id_functor(rel.id)) && !relation_yamls.any(id_functor(rel.id))) {
                    relations.push_back(std::move(rel));
                    last = false;
                }
            }
            relation_yamls.push_back(RelationYaml(relation->id, std::move(yaml_config), std::move(relation.value())));

            if (last) phase2_done.unlock();
        }
        --phase2_running;
        if (phase2_running.load() == 0) {
            phase2_done.unlock();
        }
    }
    void phase3() {
        while (!canceled) {
            auto rel_yaml = relation_yamls.move_back();
            if (rel_yaml == boost::none) break;

            auto rel = std::move(rel_yaml->relation);
            auto yaml_config = std::move(rel_yaml->yaml_config);

            if (handlers::RelationMap::has_cache(rel)) {
                continue;
            }
            auto relmap = handlers::RelationMap(rel, yaml_config);
            Converter(yaml_config, true).run(relmap);
            handlers::RelationMap::store_cache(std::move(relmap));
        }
        --phase3_running;
        if (phase3_running.load() == 0) {
            phase3_done.unlock();
        }
    }
    void phase4() {
        while (!canceled) {
            bool last = false;
            auto yaml_config_opt = yaml_configs.move_front(&last);
            if (yaml_config_opt == boost::none) break;
            auto& yaml_config = yaml_config_opt.value();

            if (yaml_config.handler.type == YamlConfig::Handler::Type::kNone) {
                if (!arg_config.quiet) {
                    utils::log("skip: ", yaml_config.path);
                }
                continue;
            }
            auto converter = Converter(yaml_config);
            switch (yaml_config.handler.type) {
                case YamlConfig::Handler::Type::kJson: {
                    auto handler = handlers::JsonHandler(yaml_config);
                    converter.run(handler);
                    break;
                }
                case YamlConfig::Handler::Type::kDjangoFixture: {
                    auto handler = handlers::DjangoFixtureHandler(yaml_config);
                    converter.run(handler);
                    break;
                }
                case YamlConfig::Handler::Type::kCSV: {
                    auto handler = handlers::CSVHandler(yaml_config);
                    converter.run(handler);
                    break;
                }
                case YamlConfig::Handler::Type::kLua: {
                    auto handler = handlers::LuaHandler(yaml_config);
                    converter.run(handler);
                    break;
                }
                case YamlConfig::Handler::Type::kTemplate: {
                    auto handler = handlers::TemplateHandler(yaml_config);
                    converter.run(handler);
                    break;
                }
                default: {
                    throw EXCEPTION(yaml_config.path, ": handler.type=", yaml_config.handler.type_name,
                                    ": not implemented.");
                }
            }
        }
    }
};

}  // namespace anonymous

int main(int argc, char** argv)
{
    using namespace xlsxconverter;

    boost::optional<ArgConfig> arg_config;
    try {
        arg_config = ArgConfig(argc, argv);
    } catch (std::exception& exc) {
        std::cerr << exc.what() << std::endl << std::endl;
        std::cerr << ArgConfig::help() << std::endl;
        return 1;
    }

    if (arg_config->targets.empty() && arg_config->yaml_search_path == ".") {
        std::cerr << ArgConfig::help() << std::endl;
        return 1;
    }

    int jobs = arg_config->jobs;
    if (!arg_config->quiet) {
        utils::log("jobs: ", jobs);
    }

    Task task(arg_config.value(), jobs);

    // if (jobs > task.targets.size()) {
    //     jobs = task.targets.size();
    // }

    auto work = std::function<void(void)>([&]() {
        #ifndef DEBUG
        try {
        #endif
            task.run();
        #ifndef DEBUG
        } catch (std::exception& exc) {
            utils::logerr("exception: ", exc.what());
            task.canceled = true;
            task.phase1_done.unlock();
            task.phase2_done.unlock();
            task.phase3_done.unlock();
            return;
        }
        #endif
    });

    auto tasks = std::vector<std::thread>();
    for (int i = 0; i < jobs - 1; ++i) {
        tasks.emplace_back(work);
    }

    // 1 task run in current thread.
    work();

    for (int i = 0; i < jobs - 1; ++i) {
        tasks[i].join();
    }

    if (task.canceled) {
        return 1;
    }
    return 0;
}

