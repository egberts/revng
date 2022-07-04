/// \file Runner.cpp
/// \brief a step is composed of a list of pipes and a set of containers
/// rappresenting the content of the pipeline before the execution of such
/// pipes.

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "revng/Pipeline/ContainerSet.h"
#include "revng/Pipeline/Context.h"
#include "revng/Pipeline/Step.h"
#include "revng/Pipeline/Target.h"
#include "revng/Support/Assert.h"
#include "revng/Support/Debug.h"

using namespace llvm;
using namespace std;
using namespace pipeline;

ContainerToTargetsMap
Step::analyzeGoals(const ContainerToTargetsMap &RequiredGoals,
                   ContainerToTargetsMap &AlreadyAviable) const {

  ContainerToTargetsMap Targets = RequiredGoals;
  removeSatisfiedGoals(Targets, AlreadyAviable);
  for (const auto &Pipe : llvm::make_range(Pipes.rbegin(), Pipes.rend())) {
    Targets = Pipe->getRequirements(Targets);
  }

  return Targets;
}

void Step::explainStartStep(const ContainerToTargetsMap &Targets,
                            size_t Indentation) const {

  indent(ExplanationLogger, Indentation);
  ExplanationLogger << "STARTING step on containers\n";
  indent(ExplanationLogger, Indentation + 1);
  ExplanationLogger << getName() << ":\n";
  prettyPrintStatus(Targets, ExplanationLogger, Indentation + 2);
  ExplanationLogger << DoLog;
}

void Step::explainExecutedPipe(const Context &Ctx,
                               const InvokableWrapperBase &Wrapper,
                               size_t Indentation) const {
  ExplanationLogger << "RUN " << Wrapper.getName();
  ExplanationLogger << "(";

  auto Vec = Wrapper.getRunningContainersNames();
  if (not Vec.empty()) {
    for (size_t I = 0; I < Vec.size() - 1; I++) {
      ExplanationLogger << Vec[I];
      ExplanationLogger << ", ";
    }
    ExplanationLogger << Vec.back();
  }

  ExplanationLogger << ")";
  ExplanationLogger << "\n";
  ExplanationLogger << DoLog;

  auto CommandStream = CommandLogger.getAsLLVMStream();
  Wrapper.print(Ctx, *CommandStream, Indentation);
  CommandStream->flush();
  CommandLogger << DoLog;
}

ContainerSet Step::cloneAndRun(Context &Ctx, ContainerSet &&Input) {
  auto InputEnumeration = Input.enumerate();
  explainStartStep(InputEnumeration);

  for (auto &Pipe : Pipes) {
    if (not Pipe->areRequirementsMet(Input.enumerate()))
      continue;

    explainExecutedPipe(Ctx, *Pipe);
    Pipe->run(Ctx, Input);
    llvm::cantFail(Input.verify());
  }
  Containers.mergeBack(std::move(Input));
  InputEnumeration = deduceResults(InputEnumeration);
  return Containers.cloneFiltered(InputEnumeration);
}

void Step::runAnalysis(llvm::StringRef AnalysisName,
                       Context &Ctx,
                       const ContainerToTargetsMap &Targets) {
  auto Stream = ExplanationLogger.getAsLLVMStream();
  ContainerToTargetsMap Map = Containers.enumerate();
  revng_assert(Map.contains(Targets),
               "An analysis was requested, but not all targets are aviable");

  auto &TheAnalysis = getAnalysis(AnalysisName);

  explainExecutedPipe(Ctx, *TheAnalysis);

  auto Cloned = Containers.cloneFiltered(Targets);
  TheAnalysis->run(Ctx, Cloned);
}

void Step::removeSatisfiedGoals(TargetsList &RequiredInputs,
                                const ContainerBase &CachedSymbols,
                                TargetsList &ToLoad) {
  const TargetsList EnumeratedSymbols = CachedSymbols.enumerate();
  const auto IsCached = [&ToLoad,
                         &EnumeratedSymbols](const Target &Target) -> bool {
    bool MustBeLoaded = EnumeratedSymbols.contains(Target);
    if (MustBeLoaded)
      ToLoad.emplace_back(Target);
    return MustBeLoaded;
  };

  llvm::erase_if(RequiredInputs, IsCached);
}

void Step::removeSatisfiedGoals(ContainerToTargetsMap &Targets,
                                ContainerToTargetsMap &ToLoad) const {
  for (auto &RequiredInputsFromContainer : Targets) {
    llvm::StringRef ContainerName = RequiredInputsFromContainer.first();
    auto &RequiredInputs = RequiredInputsFromContainer.second;
    auto &ToLoadFromCurrentContainer = ToLoad[ContainerName];
    if (Containers.contains(ContainerName))
      removeSatisfiedGoals(RequiredInputs,
                           Containers.at(ContainerName),
                           ToLoadFromCurrentContainer);
  }
}

ContainerToTargetsMap Step::deduceResults(ContainerToTargetsMap Input) const {
  for (const auto &Pipe : Pipes)
    Input = Pipe->deduceResults(Input);
  return Input;
}

Error Step::invalidate(const ContainerToTargetsMap &ToRemove) {
  return containers().remove(ToRemove);
}

Error Step::storeToDisk(llvm::StringRef DirPath) const {
  return Containers.storeToDisk(DirPath);
}

Error Step::loadFromDisk(llvm::StringRef DirPath) {
  if (not llvm::sys::fs::exists(DirPath))
    return llvm::Error::success();
  return Containers.loadFromDisk(DirPath);
}
