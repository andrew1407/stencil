using System.Net;
using Stencil.TelegramBot.Infrastructure.Configuration;
using StackExchange.Redis;

namespace Stencil.TelegramBot.Tests.Configuration;

/// <summary><c>REDIS_URL</c> parsing: the <c>redis://</c> URL form <c>bot/.env.example</c> documents (and hosted Redis hands out) alongside StackExchange's native configuration string.</summary>
public sealed class RedisConnectionStringTests
{
    private static DnsEndPoint endpointOf(ConfigurationOptions options) =>
        Assert.IsType<DnsEndPoint>(Assert.Single(options.EndPoints));

    [Fact]
    public void Should_Parse_Host_Port_And_Database_From_The_Documented_Url_Form()
    {
        ConfigurationOptions options = RedisConnectionString.Parse("redis://localhost:6379/0");

        DnsEndPoint endpoint = endpointOf(options);
        Assert.Equal("localhost", endpoint.Host);
        Assert.Equal(6379, endpoint.Port);
        Assert.Equal(0, options.DefaultDatabase);
        Assert.False(options.Ssl);
    }

    [Fact]
    public void Should_Use_The_Redis_Default_Port_For_A_Url_Without_A_Port()
    {
        Assert.Equal(RedisConnectionString.DEFAULT_PORT, endpointOf(RedisConnectionString.Parse("redis://cache.internal")).Port);
    }

    [Fact]
    public void Should_Turn_On_Tls_For_The_Rediss_Scheme()
    {
        ConfigurationOptions options = RedisConnectionString.Parse("rediss://cache.example:6380/2");

        Assert.True(options.Ssl);
        Assert.Equal(6380, endpointOf(options).Port);
        Assert.Equal(2, options.DefaultDatabase);
    }

    [Fact]
    public void Should_Take_The_User_And_Password_From_User_Info()
    {
        ConfigurationOptions withBoth = RedisConnectionString.Parse("redis://alice:s3cr3t@cache:6379/1");
        Assert.Equal("alice", withBoth.User);
        Assert.Equal("s3cr3t", withBoth.Password);
        Assert.Equal(1, withBoth.DefaultDatabase);

        // The common password-only form (no ACL user) leaves User unset.
        ConfigurationOptions passwordOnly = RedisConnectionString.Parse("redis://:s3cr3t@cache");
        Assert.Null(passwordOnly.User);
        Assert.Equal("s3cr3t", passwordOnly.Password);
    }

    [Fact]
    public void Should_Preserve_An_Escaped_Password_With_The_Characters_A_Configuration_String_Would_Eat()
    {
        // ',' separates options and '=' separates key from value in the native syntax — a
        // password holding either only survives because it is set on the options object.
        ConfigurationOptions options = RedisConnectionString.Parse("redis://:p%40ss%2Cwo%3Drd@cache");

        Assert.Equal("p@ss,wo=rd", options.Password);
    }

    [Fact]
    public void Should_Accept_The_Native_Configuration_String()
    {
        ConfigurationOptions options = RedisConnectionString.Parse("cache:6380,defaultDatabase=3,abortConnect=false");

        Assert.Equal(6380, endpointOf(options).Port);
        Assert.Equal(3, options.DefaultDatabase);
        Assert.False(options.AbortOnConnectFail);
    }

    [Fact]
    public void Should_Reject_An_Unusable_Value_Without_Quoting_It()
    {
        // The value can carry a password, so the message must describe the forms, not echo it.
        ArgumentException ex = Assert.Throws<ArgumentException>(
            () => RedisConnectionString.Parse("redis://:hunter2@"));

        Assert.DoesNotContain("hunter2", ex.Message);
        Assert.Contains("redis://", ex.Message);
    }
}
