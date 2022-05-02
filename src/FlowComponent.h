#pragma once

class FlowComponent
{
  public:
	FlowComponent* addTo(tf::Taskflow& composition)
	{
		compositionTask_ = composition.composed_of(componentFlow_);
		return this;
	}
	FlowComponent* precede(FlowComponent& successor)
	{
		return precede(&successor);
	}
	FlowComponent* precede(FlowComponent* successor)
	{
		assert(successor);
		compositionTask_.precede(successor->compositionTask_);
		return this;
	}
	FlowComponent* name(const std::string& name)
	{
		compositionTask_.name(name);
		return this;
	}
	tf::Task& nextTask()
	{
		componentTasks_.push_back(componentFlow_.placeholder());
		return componentTasks_.back();
	}
	tf::Task& getTask(size_t index)
	{
		if (index >= componentTasks_.size())
			return *componentTasks_.end();

		return componentTasks_[index];
	}

  private:
	std::vector<tf::Task> componentTasks_;
	tf::Taskflow componentFlow_;
	tf::Task compositionTask_;
};
