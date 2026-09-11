using Stencil.TelegramBot.Domain.Projects;

namespace Stencil.TelegramBot.Application.Servers;

// Carries the origin so an aggregated cross-server listing keeps each project addressable —
// the browser and pystencil tag a multi-server list the same way.
public sealed record ServerProjectInfo(ProjectRecord Record, string ServerUrl);
