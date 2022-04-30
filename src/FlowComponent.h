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
		componentTasks_.push(componentFlow_.placeholder());
		return componentTasks_.back();
	}

  private:
	std::queue<tf::Task> componentTasks_;
	tf::Taskflow componentFlow_;
	tf::Task compositionTask_;
};
